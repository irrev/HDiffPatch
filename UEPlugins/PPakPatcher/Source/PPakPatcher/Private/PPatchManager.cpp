// Copyright (c) Tencent. All rights reserved.
#include "PPatchManager.h"

#include "Data/PPatchManifestFile.h"
#include "Data/PUpdateManifestSummary.h"
#include "Data/PPakPatcherDataType.h"   // LogPPakPacher
#include "Patcher/FPResPatcher.h"
#include "Utils/PPakPatcherUtils.h"

#include "Misc/LazySingleton.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace
{
	/**
	 * 从文件名（含/不含路径都可）中提取 chunk 标识：
	 *   "pakchunk0_1.19.9.3_3_P.pak"            -> "pakchunk0"
	 *   "pakchunk1001optional_1.19.9.2_2_P.pak" -> "pakchunk1001optional"
	 *   "pakchunk1001_0.pak"                    -> "pakchunk1001"
	 *
	 * 规则：以"第一段下划线之后立即出现数字"作为切分点，前缀即 chunk 名。
	 * 找不到符合规则的切分点时返回 base name（fallback；保证不会因为命名变化导致整批丢失）。
	 */
	FString ExtractChunkNameImpl(const FString& InFilename)
	{
		const FString Base = FPaths::GetBaseFilename(InFilename); // 去掉目录与扩展
		if (Base.IsEmpty())
		{
			return FString();
		}

		const TCHAR* Ptr = *Base;
		const int32 Len  = Base.Len();
		for (int32 i = 0; i < Len - 1; ++i)
		{
			if (Ptr[i] == TEXT('_') && FChar::IsDigit(Ptr[i + 1]))
			{
				return Base.Left(i);
			}
		}
		// fallback：找不到合规分隔，整个 base name 作为 chunk 名
		return Base;
	}

	/** 收集目录下所有 .pak 文件（递归不必要：UE Cook 产物 Paks 只有一层）。 */
	TArray<FString> GatherPaks(const FString& InDir)
	{
		TArray<FString> Result;
		IFileManager::Get().FindFilesRecursive(Result, *InDir, TEXT("*.pak"), /*Files*/ true, /*Dirs*/ false);
		return Result;
	}

	/** 在 OldDir 中按 chunk 名构建 {chunkName -> 绝对路径} 映射。 */
	TMap<FString, FString> BuildOldChunkMap(const FString& InOldDir)
	{
		TMap<FString, FString> Map;
		const TArray<FString> OldPaks = GatherPaks(InOldDir);
		for (const FString& Path : OldPaks)
		{
			const FString ChunkName = ExtractChunkNameImpl(Path);
			if (ChunkName.IsEmpty())
			{
				continue;
			}
			// 同 chunk 多个 .pak 时取后一个（现实中不应出现）；打 warning
			if (Map.Contains(ChunkName))
			{
				UE_LOG(LogPPakPacher, Warning,
					TEXT("FPPatchManager - Multiple paks share chunk name '%s' in OldDir; later wins. Existing:%s New:%s"),
					*ChunkName, *Map.FindChecked(ChunkName), *Path);
			}
			Map.Add(ChunkName, Path);
		}
		return Map;
	}

	/**
	 * .pak 的 IoStore 同伴文件名后缀。
	 *   .pak 是 Legacy 资源容器；UE5 cook 产物常态：一个 .pak 同时携带 .utoc 与 .ucas。
	 */
	static const TCHAR* GIoStoreCompanionExts[] = { TEXT("utoc"), TEXT("ucas") };

	/**
	 * 把 .pak 的 IoStore 同伴文件（.utoc/.ucas）从源目录拷到目标目录。
	 *   - 仅对 .pak 文件触发；其它扩展名直接返回 true（无需拷贝）
	 *   - 同伴文件不存在视为正常（不是所有 chunk 都有 IoStore），跳过
	 *   - 拷贝失败任一一个即返回 false
	 *
	 * 设计要点：与 Modify 路径对称——Modify 通过 patch 文件内嵌 IoStoreBody 携带同伴；
	 * Add 则通过物理拷贝携带同伴（无 diff 可言，整文件拷过去）。
	 *
	 * @param InContextTagForLog 日志前缀（如 "CreatePatch.Add" / "ApplyPatch.Add"）
	 */
	bool CopyIoStoreCompanions(const FString& InPakFileName,
		const FString& InSrcDir, const FString& InDstDir,
		const TCHAR* InContextTagForLog)
	{
		if (!FPaths::GetExtension(InPakFileName).Equals(TEXT("pak"), ESearchCase::IgnoreCase))
		{
			return true;
		}
		const FString BaseName = FPaths::GetBaseFilename(InPakFileName);
		bool bAllOk = true;
		for (const TCHAR* Ext : GIoStoreCompanionExts)
		{
			const FString CompanionName = FString::Printf(TEXT("%s.%s"), *BaseName, Ext);
			const FString From = InSrcDir / CompanionName;
			const FString To   = InDstDir / CompanionName;
			if (!IFileManager::Get().FileExists(*From))
			{
				continue;   // 没有该同伴文件不视为错（并非所有 chunk 都有 IoStore）
			}
			if (COPY_OK != IFileManager::Get().Copy(*To, *From))
			{
				UE_LOG(LogPPakPacher, Error,
					TEXT("FPPatchManager::%s - Copy companion failed: %s -> %s"),
					InContextTagForLog, *From, *To);
				bAllOk = false;
			}
			else
			{
				UE_LOG(LogPPakPacher, Display,
					TEXT("FPPatchManager::%s - Copy companion: %s -> %s"),
					InContextTagForLog, *From, *To);
			}
		}
		return bAllOk;
	}
}

FPPatchManager& FPPatchManager::Get()
{
	return TLazySingleton<FPPatchManager>::Get();
}

void FPPatchManager::TearDown()
{
	TLazySingleton<FPPatchManager>::TearDown();
}

FString FPPatchManager::ExtractChunkName(const FString& InFilename)
{
	return ExtractChunkNameImpl(InFilename);
}

FString FPPatchManager::FindOldFileForNew(const FString& InNewFileName,
	const TMap<FString, FString>& InOldFileMap)
{
	const FString ChunkName = ExtractChunkNameImpl(InNewFileName);
	if (ChunkName.IsEmpty())
	{
		return FString();
	}
	if (const FString* Found = InOldFileMap.Find(ChunkName))
	{
		return *Found;
	}
	return FString();
}

bool FPPatchManager::CheckCompatibility(const FPPatchManifestFile& InPatchManifest,
	const FPUpdateManifestSummary& InResSummary) const
{
	bool bOk = true;
	auto Mismatch = [&bOk](const TCHAR* Field, const FString& Expect, const FString& Actual)
		{
			UE_LOG(LogPPakPacher, Error,
				TEXT("FPPatchManager - Compatibility check failed: %s mismatch. Expect=[%s] Actual=[%s]"),
				Field, *Expect, *Actual);
			bOk = false;
		};

	if (InPatchManifest.OldAppVersion != InResSummary.AppVersion)
	{
		Mismatch(TEXT("AppVersion"), InPatchManifest.OldAppVersion, InResSummary.AppVersion);
	}
	if (InPatchManifest.OldResVersion != InResSummary.ResVersion)
	{
		Mismatch(TEXT("ResVersion"), InPatchManifest.OldResVersion, InResSummary.ResVersion);
	}
	if (InPatchManifest.Platform != InResSummary.Platform)
	{
		Mismatch(TEXT("Platform"), InPatchManifest.Platform, InResSummary.Platform);
	}
	if (InPatchManifest.DolphinChannelID != InResSummary.DolphinChannelID)
	{
		Mismatch(TEXT("DolphinChannelID"), InPatchManifest.DolphinChannelID, InResSummary.DolphinChannelID);
	}
	if (InPatchManifest.PufferChannelID != InResSummary.PufferChannelID)
	{
		Mismatch(TEXT("PufferChannelID"), InPatchManifest.PufferChannelID, InResSummary.PufferChannelID);
	}
	return bOk;
}

// =========================================================================
// CreatePatch
// =========================================================================

bool FPPatchManager::CreatePatch(const FString& InOldDir, const FString& InNewDir, const FString& InPatchDir)
{
	UE_LOG(LogPPakPacher, Display, TEXT("FPPatchManager::CreatePatch - Begin. Old:%s New:%s Patch:%s"),
		*InOldDir, *InNewDir, *InPatchDir);

	// step 1 : 加载新旧资源 manifest
	FPUpdateManifestSummary OldSummary, NewSummary;
	if (!OldSummary.Load(InOldDir / GetSourceManifestFileName())) return false;
	if (!NewSummary.Load(InNewDir / GetSourceManifestFileName())) return false;

	// step 2 : 平台一致性
	if (OldSummary.Platform != NewSummary.Platform)
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPatchManager::CreatePatch - Platform mismatch: Old=%s New=%s"),
			*OldSummary.Platform, *NewSummary.Platform);
		return false;
	}

	// step 3 : 准备输出目录
	IFileManager::Get().MakeDirectory(*InPatchDir, /*Tree*/ true);

	// step 4 : 构建 OldDir {chunkName -> pak路径} 映射
	const TMap<FString, FString> OldChunkMap = BuildOldChunkMap(InOldDir);

	// step 5 : 创建 patch_manifest 元信息
	FPPatchManifestFile PatchManifest;
	PatchManifest.OldAppVersion    = OldSummary.AppVersion;
	PatchManifest.OldResVersion    = OldSummary.ResVersion;
	PatchManifest.NewAppVersion    = NewSummary.AppVersion;
	PatchManifest.NewResVersion    = NewSummary.ResVersion;
	PatchManifest.Platform         = NewSummary.Platform;
	PatchManifest.DolphinChannelID = NewSummary.DolphinChannelID;
	PatchManifest.PufferChannelID  = NewSummary.PufferChannelID;

	FPResPatcher ResPatcher;
	int32 NumModify = 0, NumAdd = 0, NumEqual = 0, NumDelete = 0, NumFailed = 0;

	// ---- Helper: 填充一个 Entry 的 hash 信息 ----
	auto FillEntryHash = [](FPPatchManifestFileEntry& OutEntry, const FString& InFilePath)
	{
		OutEntry.FileName = FPaths::GetCleanFilename(InFilePath);
		OutEntry.NewMD5   = FPPakPatcherUtils::CalculateFileMD5String(InFilePath);
		OutEntry.NewCRC   = FPPakPatcherUtils::CalculateFileCrc32(InFilePath);
		OutEntry.NewSize  = IFileManager::Get().FileSize(*InFilePath);
	};
	auto FillEntryOldHash = [](FPPatchManifestFileEntry& OutEntry, const FString& InFilePath)
	{
		OutEntry.FileName = FPaths::GetCleanFilename(InFilePath);
		OutEntry.OldMD5   = FPPakPatcherUtils::CalculateFileMD5String(InFilePath);
		OutEntry.OldCRC   = FPPakPatcherUtils::CalculateFileCrc32(InFilePath);
		OutEntry.OldSize  = IFileManager::Get().FileSize(*InFilePath);
	};

	// step 6 : 遍历 New .pak，按 chunk 匹配 Old 生成补丁
	TSet<FString> ProcessedOldChunks;
	for (const TPair<FString, FPUpdateManifestSummaryItem>& KV : NewSummary.GetManifestFileItems())
	{
		const FString& NewFileName = KV.Key;

		// 只处理 .pak（IoStore 同伴在下面一并处理）
		if (!FPaths::GetExtension(NewFileName).Equals(TEXT("pak"), ESearchCase::IgnoreCase))
			continue;

		const FString ChunkName = ExtractChunkNameImpl(NewFileName);
		const FString BaseName  = FPaths::GetBaseFilename(NewFileName);
		const FString* OldPakPath = OldChunkMap.Find(ChunkName);

		FPPatchManifestFileItem Item;
		Item.ChunkName = ChunkName;

		// ---- 收集各文件路径 ----
		const FString NewPak  = InNewDir / NewFileName;
		const FString NewUtoc = InNewDir / FString::Printf(TEXT("%s.utoc"), *BaseName);
		const FString NewUcas = InNewDir / FString::Printf(TEXT("%s.ucas"), *BaseName);

		const bool bNewUtocExists = IFileManager::Get().FileExists(*NewUtoc);
		const bool bNewUcasExists = IFileManager::Get().FileExists(*NewUcas);

		if (!OldPakPath)
		{
			// ═══ Add ═══
			Item.Pak.FileName = NewFileName;
			Item.Pak.DiffType = EPFileCompareDiffType::Add;
			FillEntryHash(Item.Pak, NewPak);

			// 拷贝 .pak 到 PatchDir
			if (COPY_OK != IFileManager::Get().Copy(*(InPatchDir / NewFileName), *NewPak))
			{
				UE_LOG(LogPPakPacher, Error, TEXT("CreatePatch - Add copy .pak failed: %s"), *NewPak);
				++NumFailed; continue;
			}

			// IoStore 同伴
			if (bNewUtocExists)
			{
				Item.Utoc.DiffType = EPFileCompareDiffType::Add;
				FillEntryHash(Item.Utoc, NewUtoc);
				IFileManager::Get().Copy(*(InPatchDir / FPaths::GetCleanFilename(NewUtoc)), *NewUtoc);
			}
			if (bNewUcasExists)
			{
				Item.Ucas.DiffType = EPFileCompareDiffType::Add;
				FillEntryHash(Item.Ucas, NewUcas);
				IFileManager::Get().Copy(*(InPatchDir / FPaths::GetCleanFilename(NewUcas)), *NewUcas);
			}

			PatchManifest.AddItem(Item);
			++NumAdd;
			continue;
		}

		// ═══ 有对应 Old chunk ═══
		ProcessedOldChunks.Add(ChunkName);

		const FString OldPak  = *OldPakPath;
		const FString OldBaseName = FPaths::GetBaseFilename(OldPak);
		const FString OldUtoc = InOldDir / FString::Printf(TEXT("%s.utoc"), *OldBaseName);
		const FString OldUcas = InOldDir / FString::Printf(TEXT("%s.ucas"), *OldBaseName);
		const bool bOldUtocExists = IFileManager::Get().FileExists(*OldUtoc);
		const bool bOldUcasExists = IFileManager::Get().FileExists(*OldUcas);

		// ---- .pak 的 DiffType ----
		Item.Pak.FileName = NewFileName;
		FillEntryHash(Item.Pak, NewPak);
		FillEntryOldHash(Item.Pak, OldPak);
		// 合并 New hash 到同一 Entry（FillEntryOldHash 只写 Old*，需要把 New 也填上）
		Item.Pak.NewMD5  = FPPakPatcherUtils::CalculateFileMD5String(NewPak);
		Item.Pak.NewCRC  = FPPakPatcherUtils::CalculateFileCrc32(NewPak);
		Item.Pak.NewSize = IFileManager::Get().FileSize(*NewPak);

		if (Item.Pak.OldMD5 == Item.Pak.NewMD5)
			Item.Pak.DiffType = EPFileCompareDiffType::Equal;
		else
			Item.Pak.DiffType = EPFileCompareDiffType::Modify;

		// ---- .utoc 的 DiffType ----
		if (bNewUtocExists && bOldUtocExists)
		{
			Item.Utoc.FileName = FPaths::GetCleanFilename(NewUtoc);
			Item.Utoc.OldMD5  = FPPakPatcherUtils::CalculateFileMD5String(OldUtoc);
			Item.Utoc.OldCRC  = FPPakPatcherUtils::CalculateFileCrc32(OldUtoc);
			Item.Utoc.OldSize = IFileManager::Get().FileSize(*OldUtoc);
			Item.Utoc.NewMD5  = FPPakPatcherUtils::CalculateFileMD5String(NewUtoc);
			Item.Utoc.NewCRC  = FPPakPatcherUtils::CalculateFileCrc32(NewUtoc);
			Item.Utoc.NewSize = IFileManager::Get().FileSize(*NewUtoc);
			Item.Utoc.DiffType = (Item.Utoc.OldMD5 == Item.Utoc.NewMD5)
				? EPFileCompareDiffType::Equal : EPFileCompareDiffType::Modify;
		}
		else if (bNewUtocExists && !bOldUtocExists)
		{
			Item.Utoc.DiffType = EPFileCompareDiffType::Add;
			FillEntryHash(Item.Utoc, NewUtoc);
		}
		else if (!bNewUtocExists && bOldUtocExists)
		{
			Item.Utoc.DiffType = EPFileCompareDiffType::Delete;
			FillEntryOldHash(Item.Utoc, OldUtoc);
		}

		// ---- .ucas 的 DiffType ----
		if (bNewUcasExists && bOldUcasExists)
		{
			Item.Ucas.FileName = FPaths::GetCleanFilename(NewUcas);
			Item.Ucas.OldMD5  = FPPakPatcherUtils::CalculateFileMD5String(OldUcas);
			Item.Ucas.OldCRC  = FPPakPatcherUtils::CalculateFileCrc32(OldUcas);
			Item.Ucas.OldSize = IFileManager::Get().FileSize(*OldUcas);
			Item.Ucas.NewMD5  = FPPakPatcherUtils::CalculateFileMD5String(NewUcas);
			Item.Ucas.NewCRC  = FPPakPatcherUtils::CalculateFileCrc32(NewUcas);
			Item.Ucas.NewSize = IFileManager::Get().FileSize(*NewUcas);
			Item.Ucas.DiffType = (Item.Ucas.OldMD5 == Item.Ucas.NewMD5)
				? EPFileCompareDiffType::Equal : EPFileCompareDiffType::Modify;
		}
		else if (bNewUcasExists && !bOldUcasExists)
		{
			Item.Ucas.DiffType = EPFileCompareDiffType::Add;
			FillEntryHash(Item.Ucas, NewUcas);
		}
		else if (!bNewUcasExists && bOldUcasExists)
		{
			Item.Ucas.DiffType = EPFileCompareDiffType::Delete;
			FillEntryOldHash(Item.Ucas, OldUcas);
		}

		// ---- 判定整体 chunk DiffType ----
		const EPFileCompareDiffType ChunkType = Item.GetChunkDiffType();
		if (ChunkType == EPFileCompareDiffType::Equal)
		{
			PatchManifest.AddItem(Item);
			++NumEqual;
			continue;
		}

		// ═══ Modify（至少有一个子文件变了）═══
		// 生成 .patch 文件（FPResPatcher::CreateDiff 内部联动 IoStore）
		const FString PatchBaseName = BaseName + TEXT(".patch");
		const FString PatchFullPath = InPatchDir / PatchBaseName;

		FPResPatchDataPtr OutPatch;
		const bool bOk = ResPatcher.CreateDiff(PatchFullPath, NewPak, OldPak, OutPatch,
			EPPakPatchMode::PakAware, EPakPatchCompressType::None);
		if (!bOk)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("CreatePatch - CreateDiff failed: %s"), *PatchFullPath);
			++NumFailed;
			continue;
		}

		Item.PatchFileName = PatchBaseName;
		PatchManifest.AddItem(Item);
		++NumModify;
	}

	// step 7 : Old 中存在但 New 中没有的 chunk → Delete
	for (const TPair<FString, FString>& KV : OldChunkMap)
	{
		if (ProcessedOldChunks.Contains(KV.Key)) continue;

		const FString OldPakFile = KV.Value;
		const FString OldBaseName = FPaths::GetBaseFilename(OldPakFile);

		FPPatchManifestFileItem Item;
		Item.ChunkName = KV.Key;

		Item.Pak.DiffType = EPFileCompareDiffType::Delete;
		FillEntryOldHash(Item.Pak, OldPakFile);

		const FString OldUtoc = InOldDir / FString::Printf(TEXT("%s.utoc"), *OldBaseName);
		const FString OldUcas = InOldDir / FString::Printf(TEXT("%s.ucas"), *OldBaseName);
		if (IFileManager::Get().FileExists(*OldUtoc))
		{
			Item.Utoc.DiffType = EPFileCompareDiffType::Delete;
			FillEntryOldHash(Item.Utoc, OldUtoc);
		}
		if (IFileManager::Get().FileExists(*OldUcas))
		{
			Item.Ucas.DiffType = EPFileCompareDiffType::Delete;
			FillEntryOldHash(Item.Ucas, OldUcas);
		}

		PatchManifest.AddItem(Item);
		++NumDelete;
	}

	// step 8 : 保存 patch_manifest.txt
	if (!PatchManifest.Save(InPatchDir / GetPatchManifestFileName()))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("CreatePatch - Save manifest failed"));
		return false;
	}

	UE_LOG(LogPPakPacher, Display,
		TEXT("FPPatchManager::CreatePatch - Done. Modify=%d Add=%d Equal=%d Delete=%d Failed=%d"),
		NumModify, NumAdd, NumEqual, NumDelete, NumFailed);

	return NumFailed == 0;
}

// =========================================================================
// ApplyPatch
// =========================================================================

bool FPPatchManager::ApplyPatch(const FString& InResDir, const FString& InPatchDir)
{
	UE_LOG(LogPPakPacher, Display, TEXT("FPPatchManager::ApplyPatch - Begin. Res:%s Patch:%s"),
		*InResDir, *InPatchDir);

	FPPatchManifestFile PatchManifest;
	if (!PatchManifest.Load(InPatchDir / GetPatchManifestFileName())) return false;

	FPUpdateManifestSummary ResSummary;
	if (!ResSummary.Load(InResDir / GetSourceManifestFileName())) return false;

	if (!CheckCompatibility(PatchManifest, ResSummary))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("ApplyPatch - Compatibility check failed; abort."));
		return false;
	}

	// ---- Helper lambdas for single-file operations ----
	auto DoAdd = [&](const FPPatchManifestFileEntry& Entry, const TCHAR* Tag) -> bool
	{
		if (!Entry.IsValid()) return true;
		const FString From = InPatchDir / Entry.FileName;
		const FString To   = InResDir   / Entry.FileName;
		if (COPY_OK != IFileManager::Get().Copy(*To, *From))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("ApplyPatch.%s - Add copy failed: %s -> %s"), Tag, *From, *To);
			return false;
		}
		UE_LOG(LogPPakPacher, Display, TEXT("ApplyPatch.%s - Add: %s"), Tag, *Entry.FileName);
		return true;
	};

	auto DoDelete = [&](const FPPatchManifestFileEntry& Entry, const TCHAR* Tag) -> bool
	{
		if (!Entry.IsValid()) return true;
		const FString Target = InResDir / Entry.FileName;
		if (IFileManager::Get().FileExists(*Target))
		{
			IFileManager::Get().Delete(*Target, false);
			UE_LOG(LogPPakPacher, Display, TEXT("ApplyPatch.%s - Delete: %s"), Tag, *Target);
		}
		return true;
	};

	FPResPatcher ResPatcher;
	int32 NumModify = 0, NumAdd = 0, NumEqual = 0, NumDelete = 0, NumFailed = 0;

	for (const TPair<FString, FPPatchManifestFileItem>& KV : PatchManifest.GetManifestFileItems())
	{
		const FPPatchManifestFileItem& Item = KV.Value;
		const EPFileCompareDiffType ChunkType = Item.GetChunkDiffType();

		if (ChunkType == EPFileCompareDiffType::Equal || ChunkType == EPFileCompareDiffType::None)
		{
			++NumEqual;
			continue;
		}

		if (ChunkType == EPFileCompareDiffType::Add)
		{
			bool bOk = DoAdd(Item.Pak, TEXT("Pak")) && DoAdd(Item.Utoc, TEXT("Utoc")) && DoAdd(Item.Ucas, TEXT("Ucas"));
			if (!bOk) { ++NumFailed; continue; }
			++NumAdd;
			continue;
		}

		if (ChunkType == EPFileCompareDiffType::Delete)
		{
			DoDelete(Item.Pak, TEXT("Pak"));
			DoDelete(Item.Utoc, TEXT("Utoc"));
			DoDelete(Item.Ucas, TEXT("Ucas"));
			++NumDelete;
			continue;
		}

		// ═══ Modify（至少有一个子文件变了）═══
		if (Item.PatchFileName.IsEmpty() || !Item.Pak.IsValid())
		{
			UE_LOG(LogPPakPacher, Error, TEXT("ApplyPatch - Modify chunk [%s] missing PatchFileName or Pak entry."), *Item.ChunkName);
			++NumFailed;
			continue;
		}

		const FString PakFileName = Item.Pak.FileName;
		const FString OldFullPath = InResDir / PakFileName;
		const bool bSameName = true;  // 在当前设计下 pak 文件名不变

		// New 产物写到 .new 后缀
		const FString NewFullPath = OldFullPath + TEXT(".new");
		const FString PatchFullPath = InPatchDir / Item.PatchFileName;

		FPResPatchDataPtr Patch = MakeShared<FPResPatchData, ESPMode::ThreadSafe>();
		if (!Patch->LoadFromFile(PatchFullPath))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("ApplyPatch - Patch load failed: %s"), *PatchFullPath);
			++NumFailed;
			continue;
		}

		if (!ResPatcher.PatchDiff(NewFullPath, OldFullPath, Patch))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("ApplyPatch - PatchDiff failed: %s"), *Item.ChunkName);
			++NumFailed;
			// Cleanup .new
			IFileManager::Get().Delete(*NewFullPath, false);
			if (Item.Utoc.IsValid())
			{
				IFileManager::Get().Delete(*(InResDir / Item.Utoc.FileName + TEXT(".new")), false);
			}
			if (Item.Ucas.IsValid())
			{
				IFileManager::Get().Delete(*(InResDir / Item.Ucas.FileName + TEXT(".new")), false);
			}
			continue;
		}

		// 释放文件句柄
		Patch.Reset();
		ResPatcher = FPResPatcher();

		// ── 逐文件：Delete Old + Rename .new → 原名 ──
		auto ReplaceFile = [&](const FPPatchManifestFileEntry& Entry) -> bool
		{
			if (Entry.DiffType == EPFileCompareDiffType::Modify)
			{
				const FString OldFile = InResDir / Entry.FileName;
				const FString NewFile = OldFile + TEXT(".new");
				if (!IFileManager::Get().FileExists(*NewFile))
				{
					UE_LOG(LogPPakPacher, Error, TEXT("ApplyPatch - .new not found: %s"), *NewFile);
					return false;
				}
				IFileManager::Get().Delete(*OldFile, false);
				UE_LOG(LogPPakPacher, Display, TEXT("ApplyPatch - Delete Old: %s"), *OldFile);
				if (!IFileManager::Get().Move(*OldFile, *NewFile, true))
				{
					UE_LOG(LogPPakPacher, Error, TEXT("ApplyPatch - Rename failed: %s -> %s"), *NewFile, *OldFile);
					return false;
				}
				UE_LOG(LogPPakPacher, Display, TEXT("ApplyPatch - Rename: %s -> %s"), *NewFile, *OldFile);
			}
			else if (Entry.DiffType == EPFileCompareDiffType::Equal)
			{
				// PatchDiff 可能仍写出了 .new 文件（PatchPak 整体产出），清理残留
				const FString NewFile = InResDir / Entry.FileName + TEXT(".new");
				if (IFileManager::Get().FileExists(*NewFile))
				{
					IFileManager::Get().Delete(*NewFile, false);
					UE_LOG(LogPPakPacher, Display, TEXT("ApplyPatch - Cleanup Equal .new: %s"), *NewFile);
				}
			}
			else if (Entry.DiffType == EPFileCompareDiffType::Add)
			{
				// IoStore 同伴可能是新增的（Old 没有同伴但 New 有）
				// PatchDiff 内部应该已经产出了 .new 文件
				const FString NewFile = InResDir / Entry.FileName + TEXT(".new");
				const FString FinalFile = InResDir / Entry.FileName;
				if (IFileManager::Get().FileExists(*NewFile))
				{
					IFileManager::Get().Move(*FinalFile, *NewFile, true);
					UE_LOG(LogPPakPacher, Display, TEXT("ApplyPatch - Add (from .new): %s"), *FinalFile);
				}
			}
			else if (Entry.DiffType == EPFileCompareDiffType::Delete)
			{
				const FString Target = InResDir / Entry.FileName;
				IFileManager::Get().Delete(*Target, false);
				UE_LOG(LogPPakPacher, Display, TEXT("ApplyPatch - Delete companion: %s"), *Target);
			}
			return true;
		};

		bool bAllOk = ReplaceFile(Item.Pak);
		if (Item.Utoc.IsValid()) bAllOk &= ReplaceFile(Item.Utoc);
		if (Item.Ucas.IsValid()) bAllOk &= ReplaceFile(Item.Ucas);

		if (!bAllOk) { ++NumFailed; continue; }
		++NumModify;
	}

	UE_LOG(LogPPakPacher, Display,
		TEXT("FPPatchManager::ApplyPatch - Done. Modify=%d Add=%d Equal=%d Delete=%d Failed=%d"),
		NumModify, NumAdd, NumEqual, NumDelete, NumFailed);

	return NumFailed == 0;
}

// =========================================================================
// VerifyBeforePatch / VerifyAfterPatch
// =========================================================================
//
// 设计要点：
//   - 函数本身不读磁盘、不计算 CRC；CRC 来源完全依赖 InActualCrcMap（由资源更新层
//     在下载/校验流程里已经算好的结果），避免重复计算的开销。
//   - 唯一会读磁盘的是 patch_manifest.txt（小文本，开销可忽略）。
//   - 两个函数共用 VerifyCrcInternal，只在"取 New/Old"参数上有差异。

bool FPPatchManager::VerifyBeforePatch(const FString& InPatchDir,
	const TMap<FString, uint32>& InActualCrcMap, bool bAllowMissing) const
{
	return VerifyCrcInternal(InPatchDir, InActualCrcMap, bAllowMissing,
		/*bUseNewSide*/ false, TEXT("VerifyBeforePatch"));
}

bool FPPatchManager::VerifyAfterPatch(const FString& InPatchDir,
	const TMap<FString, uint32>& InActualCrcMap, bool bAllowMissing) const
{
	return VerifyCrcInternal(InPatchDir, InActualCrcMap, bAllowMissing,
		/*bUseNewSide*/ true, TEXT("VerifyAfterPatch"));
}

bool FPPatchManager::VerifyCrcInternal(const FString& InPatchDir,
	const TMap<FString, uint32>& InActualCrcMap,
	bool bAllowMissing,
	bool bUseNewSide,
	const TCHAR* InTagForLog) const
{
	UE_LOG(LogPPakPacher, Display,
		TEXT("FPPatchManager::%s - Begin. PatchDir:%s ActualCrcMap.Num=%d bAllowMissing=%d"),
		InTagForLog, *InPatchDir, InActualCrcMap.Num(), bAllowMissing ? 1 : 0);

	FPPatchManifestFile PatchManifest;
	if (!PatchManifest.Load(InPatchDir / GetPatchManifestFileName()))
	{
		return false;
	}

	int32 NumChecked = 0, NumFailed = 0, NumMissing = 0, NumSkipped = 0;

	// Helper：校验单个 Entry
	auto VerifyEntry = [&](const FPPatchManifestFileEntry& Entry)
	{
		if (!Entry.IsValid()) return;

		// 决定是否需要校验
		bool bShouldCheck = false;
		FString FileName;
		uint32 ExpectedCrc = 0;

		if (bUseNewSide)
		{
			// 打补丁后：Modify/Add/Equal 应该有 New 文件
			if (Entry.DiffType == EPFileCompareDiffType::Modify ||
				Entry.DiffType == EPFileCompareDiffType::Add ||
				Entry.DiffType == EPFileCompareDiffType::Equal)
			{
				FileName = Entry.FileName;
				ExpectedCrc = Entry.NewCRC;
				bShouldCheck = true;
			}
		}
		else
		{
			// 打补丁前：Modify/Delete 需要 Old 存在
			if (Entry.DiffType == EPFileCompareDiffType::Modify ||
				Entry.DiffType == EPFileCompareDiffType::Delete)
			{
				FileName = Entry.FileName;
				ExpectedCrc = Entry.OldCRC;
				bShouldCheck = true;
			}
		}

		if (!bShouldCheck || FileName.IsEmpty())
		{
			++NumSkipped;
			return;
		}

		if (ExpectedCrc == 0)
		{
			++NumSkipped;
			return;
		}

		const uint32* ActualPtr = InActualCrcMap.Find(FileName);
		if (!ActualPtr)
		{
			if (bAllowMissing)
			{
				++NumMissing;
				return;
			}
			UE_LOG(LogPPakPacher, Error,
				TEXT("FPPatchManager::%s - Actual CRC not provided for '%s'. Treat as failure."),
				InTagForLog, *FileName);
			++NumMissing;
			++NumFailed;
			return;
		}

		++NumChecked;
		if (*ActualPtr != ExpectedCrc)
		{
			UE_LOG(LogPPakPacher, Error,
				TEXT("FPPatchManager::%s - CRC mismatch. File:%s Expect:0x%08X Actual:0x%08X"),
				InTagForLog, *FileName, ExpectedCrc, *ActualPtr);
			++NumFailed;
		}
	};

	for (const TPair<FString, FPPatchManifestFileItem>& KV : PatchManifest.GetManifestFileItems())
	{
		const FPPatchManifestFileItem& Item = KV.Value;
		VerifyEntry(Item.Pak);
		VerifyEntry(Item.Utoc);
		VerifyEntry(Item.Ucas);
	}

	UE_LOG(LogPPakPacher, Display,
		TEXT("FPPatchManager::%s - Done. Checked=%d Failed=%d Missing=%d Skipped=%d"),
		InTagForLog, NumChecked, NumFailed, NumMissing, NumSkipped);

	return NumFailed == 0;
}
