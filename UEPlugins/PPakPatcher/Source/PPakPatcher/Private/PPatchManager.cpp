// Copyright (c) Tencent. All rights reserved.
#include "PPatchManager.h"

#include "Data/PPatchManifestFile.h"
#include "Data/PUpdateManifestSummary.h"
#include "Data/PPakPatcherDataType.h"   // LogPPakPacher
#include "Patcher/FPResPatcher.h"
#include "Utils/PPakPatcherUtils.h"
#include "PPakPatcherSettings.h"

#include "Misc/LazySingleton.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Utils/PPakPatcherPerfReport.h"
#include "Utils/PPakPatcherTaskRunner.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"

#include <atomic>

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

void FPPatchManager::GetPercentage(int32& OutCurrent, int32& OutTotal, float& OutPercentage) const
{
	OutCurrent = ProgressCurrent.load(std::memory_order_relaxed);
	OutTotal   = ProgressTotal.load(std::memory_order_relaxed);
	OutPercentage = (OutTotal > 0) ? FMath::Clamp((float)OutCurrent / (float)OutTotal, 0.0f, 1.0f) : 0.0f;
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

bool FPPatchManager::CreatePatch(const FString& InOldDir, const FString& InNewDir, const FString& InPatchDir,
	EPakPatchCompressType InCompressType)
{
	UE_LOG(LogPPakPacher, Display,
		TEXT("FPPatchManager::CreatePatch - Begin. Old:%s New:%s Patch:%s CompressType=%s PatchMode=%s PatchTaskThreadNum=%d"),
		*InOldDir, *InNewDir, *InPatchDir,
		*UEnum::GetValueAsString(InCompressType),
		*UEnum::GetValueAsString(UPPakPatcherSettings::Get().PakPatchMode),
		UPPakPatcherSettings::Get().PatchTaskThreadNum);

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

	// 写入构建参数快照（仅诊断/审计，不影响 ApplyPatch；详见 FPPatchBuildSettings）
	PatchManifest.BuildSettings.FillFromCurrentSettings();

	FPResPatcher ResPatcher;
	int32 NumModify = 0, NumAdd = 0, NumEqual = 0, NumDelete = 0, NumFailed = 0;

	// Modify 任务队列（CreateDiff 是热点，循环结束后按 PatchTaskThreadNum 并发执行）
	struct FModifyTask
	{
		FPPatchManifestFileItem Item;
		FString PatchFullPath;
		FString PatchBaseName;
		FString NewPak;
		FString OldPak;
	};
	TArray<FModifyTask> ModifyTasks;

	// ---- Helper: 填充 Entry 的 hash 信息（单 pass MD5+CRC32+Size，避免 3 次全文件 IO）----
	auto FillEntryHash = [](FPPatchManifestFileEntry& OutEntry, const FString& InFilePath)
	{
		OutEntry.FileName = FPaths::GetCleanFilename(InFilePath);
		FPPakPatcherUtils::CalculateFileHashesAndSize(InFilePath, OutEntry.NewMD5, OutEntry.NewCRC, OutEntry.NewSize);
	};
	auto FillEntryOldHash = [](FPPatchManifestFileEntry& OutEntry, const FString& InFilePath)
	{
		OutEntry.FileName = FPaths::GetCleanFilename(InFilePath);
		FPPakPatcherUtils::CalculateFileHashesAndSize(InFilePath, OutEntry.OldMD5, OutEntry.OldCRC, OutEntry.OldSize);
	};
	// 同时填充新旧两侧（避免 .pak 主分支重复扫描 NewPak）
	auto FillEntryBothHashes = [](FPPatchManifestFileEntry& OutEntry, const FString& InNewPath, const FString& InOldPath)
	{
		OutEntry.FileName = FPaths::GetCleanFilename(InNewPath);
		FPPakPatcherUtils::CalculateFileHashesAndSize(InNewPath, OutEntry.NewMD5, OutEntry.NewCRC, OutEntry.NewSize);
		FPPakPatcherUtils::CalculateFileHashesAndSize(InOldPath, OutEntry.OldMD5, OutEntry.OldCRC, OutEntry.OldSize);
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
		FillEntryBothHashes(Item.Pak, NewPak, OldPak);

		if (Item.Pak.OldMD5 == Item.Pak.NewMD5)
			Item.Pak.DiffType = EPFileCompareDiffType::Equal;
		else
			Item.Pak.DiffType = EPFileCompareDiffType::Modify;

		// ---- .utoc 的 DiffType ----
		if (bNewUtocExists && bOldUtocExists)
		{
			Item.Utoc.FileName = FPaths::GetCleanFilename(NewUtoc);
			FillEntryBothHashes(Item.Utoc, NewUtoc, OldUtoc);
			Item.Utoc.FileName = FPaths::GetCleanFilename(NewUtoc); // 防御：FillEntryBothHashes 已经按 NewUtoc 命名
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
			FillEntryBothHashes(Item.Ucas, NewUcas, OldUcas);
			Item.Ucas.FileName = FPaths::GetCleanFilename(NewUcas);
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
		// 生成 .patch 文件（FPResPatcher::CreateDiff 内部按 Mode 分发，IoStore 联动由各分支内部处理）
		const FString PatchBaseName = BaseName + TEXT(".patch");
		const FString PatchFullPath = InPatchDir / PatchBaseName;

		// 暂存 Modify task，等所有 chunk 收集完后并发执行
		Item.PatchFileName = PatchBaseName;
		FModifyTask Task;
		Task.Item          = Item;
		Task.PatchFullPath = PatchFullPath;
		Task.PatchBaseName = PatchBaseName;
		Task.NewPak        = NewPak;
		Task.OldPak        = OldPak;
		ModifyTasks.Add(MoveTemp(Task));
	}

	// step 6.5 : 并发执行所有 Modify chunk 的 CreateDiff
	FPPakPatcherPerfReport CreateSummary;
	if (ModifyTasks.Num() > 0)
	{
		const int32 ThreadNum = UPPakPatcherSettings::Get().PatchTaskThreadNum;
		FPPakPatcherTaskRunner Runner(ThreadNum, TEXT("CreatePatch"));
		const EPPakPatchMode PatchMode = UPPakPatcherSettings::Get().PakPatchMode;

		UE_LOG(LogPPakPacher, Display,
			TEXT("FPPatchManager::CreatePatch - Dispatching %d Modify tasks. PatchMode=%s CompressType=%s"),
			ModifyTasks.Num(),
			*UEnum::GetValueAsString(PatchMode),
			*UEnum::GetValueAsString(InCompressType));

		// 结果数组：每个 task 写自己的 slot，避免数据竞争
		const int32 NumTasks = ModifyTasks.Num();
		TArray<bool> TaskResults;
		TaskResults.SetNumZeroed(NumTasks);

		// 进度追踪
		ProgressCurrent.store(0, std::memory_order_relaxed);
		ProgressTotal.store(NumTasks, std::memory_order_relaxed);
		std::atomic<int32>* ProgressPtr = &ProgressCurrent;

		// 共享 PerfReport（所有 task 通过 ThreadSafeMergeFrom 汇总）

		for (int32 i = 0; i < NumTasks; ++i)
		{
			const FModifyTask& T = ModifyTasks[i];
			Runner.Submit([T, PatchMode, InCompressType, &TaskResults, i, NumTasks, ProgressPtr, &CreateSummary](int32 TaskIdx) -> bool
			{
				const double TaskStart = FPlatformTime::Seconds();
				UE_LOG(LogPPakPacher, Display,
					TEXT("CreatePatch[Task %d/%d] - Start. Chunk=%s NewPak=%s"),
					i + 1, NumTasks, *T.Item.ChunkName, *T.NewPak);

				FPPakPatcherPerfReport LocalPerf;
				FPResPatcher LocalPatcher;
				FPResPatchDataPtr OutPatch;
				const bool bOk = LocalPatcher.CreateDiff(T.PatchFullPath, T.NewPak, T.OldPak,
					OutPatch, PatchMode, InCompressType, &LocalPerf);
				const double Elapsed = FPlatformTime::Seconds() - TaskStart;
				if (!bOk)
				{
					UE_LOG(LogPPakPacher, Error,
						TEXT("CreatePatch[Task %d/%d] - FAILED. Chunk=%s Patch=%s NewPak=%s OldPak=%s Elapsed=%.2fs"),
						i + 1, NumTasks, *T.Item.ChunkName, *T.PatchFullPath, *T.NewPak, *T.OldPak, Elapsed);
				}
				else
				{
					UE_LOG(LogPPakPacher, Display,
						TEXT("CreatePatch[Task %d/%d] - Done. Chunk=%s Elapsed=%.2fs"),
						i + 1, NumTasks, *T.Item.ChunkName, Elapsed);
				}
				TaskResults[i] = bOk;
				CreateSummary.ThreadSafeMergeFrom(LocalPerf);
				ProgressPtr->fetch_add(1, std::memory_order_relaxed);
				return bOk;
			});
		}

		Runner.Wait();

		// 串行汇总结果到 PatchManifest / 计数器
		for (int32 i = 0; i < NumTasks; ++i)
		{
			if (TaskResults[i])
			{
				PatchManifest.AddItem(ModifyTasks[i].Item);
				++NumModify;
			}
			else
			{
				++NumFailed;
			}
		}
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

	// 保存 PerfReport 汇总（所有 chunk 的 PerfReport 已通过 ThreadSafeMergeFrom 累加到 CreateSummary）
	if (ModifyTasks.Num() > 0)
	{
		const FString SummaryPath = InPatchDir / TEXT("perf_summary.json");
		CreateSummary.SaveToFile(SummaryPath);
		UE_LOG(LogPPakPacher, Display,
			TEXT("CreatePatch - PerfSummary saved (%d chunks): %s"), ModifyTasks.Num(), *SummaryPath);
	}

	return NumFailed == 0;
}

// =========================================================================
// ApplyPatch
// =========================================================================

bool FPPatchManager::ApplyPatch(const FString& InResDir, const FString& InPatchDir)
{
	UE_LOG(LogPPakPacher, Display,
		TEXT("FPPatchManager::ApplyPatch - Begin. Res:%s Patch:%s PatchTaskThreadNum=%d"),
		*InResDir, *InPatchDir,
		UPPakPatcherSettings::Get().PatchTaskThreadNum);

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

	// Modify 任务队列：PatchDiff 是热点，按 PatchTaskThreadNum 并发执行
	struct FApplyTask
	{
		FPPatchManifestFileItem Item;
		FString OldFullPath;
		FString NewFullPath;
		FString PatchFullPath;
	};
	TArray<FApplyTask> ApplyTasks;

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

		// New 产物写到 .new 后缀
		const FString NewFullPath = OldFullPath + TEXT(".new");
		const FString PatchFullPath = InPatchDir / Item.PatchFileName;

		FApplyTask T;
		T.Item          = Item;
		T.OldFullPath   = OldFullPath;
		T.NewFullPath   = NewFullPath;
		T.PatchFullPath = PatchFullPath;
		ApplyTasks.Add(MoveTemp(T));
	}

	// ── 修复 #6：两阶段原子替换 .pak + .utoc + .ucas（chunk 内必须全成功或全回滚）──
	//
	// 旧策略：顺序调 ReplaceFile(Pak) → ReplaceFile(Utoc) → ReplaceFile(Ucas)，pak 成功
	// 但 utoc 失败会产生致命半状态（新 pak + 旧 utoc/ucas）。
	// 旧的 ReplaceFile 单文件 lambda 已被 ReplaceFilesAtomic 取代并删除（清理死代码）。
	//
	// 新策略：两阶段提交 + best-effort 回滚
	//   阶段 0（preflight）：检查所有 .new 文件都存在
	//   阶段 1：对每个 Modify 目标：rename OldFile → OldFile.bak；rename NewFile → OldFile
	//   阶段 2：全成功 → 删除所有 .bak；任何失败 → 回滚已成功的（rename .bak → OldFile，
	//          清理 .new 文件）
	//
	// 仅处理 chunk 内的 Modify 子集；Equal/Add/Delete 不在此函数处理（由外层 DoAdd/DoDelete
	// 单文件处理；ApplyPatch task 只面对 Modify chunks）。
	auto ReplaceFilesAtomic = [](const FString& InResDirInner, const TArray<FPPatchManifestFileEntry>& Entries) -> bool
	{
		// 收集所有 Modify entry 的目标文件路径
		struct FOp { FString OldFile; FString NewFile; FString BakFile; bool bMovedToBak = false; bool bMovedFromNew = false; };
		TArray<FOp> Ops;
		Ops.Reserve(Entries.Num());

		for (const FPPatchManifestFileEntry& Entry : Entries)
		{
			if (Entry.DiffType != EPFileCompareDiffType::Modify)
			{
				continue;
			}
			FOp Op;
			Op.OldFile = InResDirInner / Entry.FileName;
			Op.NewFile = Op.OldFile + TEXT(".new");
			Op.BakFile = Op.OldFile + TEXT(".bak");
			Ops.Add(MoveTemp(Op));
		}

		if (Ops.Num() == 0)
		{
			return true; // 没有 Modify entry 也算成功
		}

		// 阶段 0：preflight — 所有 .new 必须存在
		for (const FOp& Op : Ops)
		{
			if (!IFileManager::Get().FileExists(*Op.NewFile))
			{
				UE_LOG(LogPPakPacher, Error,
					TEXT("ApplyPatch::ReplaceFilesAtomic - preflight failed: .new not found: %s"), *Op.NewFile);
				return false;
			}
			// 清理可能残留的 .bak（上一次失败遗留）
			if (IFileManager::Get().FileExists(*Op.BakFile))
			{
				IFileManager::Get().Delete(*Op.BakFile, false, true /*bEvenReadOnly*/, true /*bQuiet*/);
			}
		}

		// 阶段 1：rename OldFile → .bak，然后 .new → OldFile
		bool bAllOk = true;
		for (FOp& Op : Ops)
		{
			// 1a. OldFile 存在则 rename 到 .bak（保留原始）
			if (IFileManager::Get().FileExists(*Op.OldFile))
			{
				if (!IFileManager::Get().Move(*Op.BakFile, *Op.OldFile, true))
				{
					UE_LOG(LogPPakPacher, Error,
						TEXT("ApplyPatch::ReplaceFilesAtomic - failed to backup OldFile: %s -> %s"),
						*Op.OldFile, *Op.BakFile);
					bAllOk = false;
					break;
				}
				Op.bMovedToBak = true;
			}
			// 1b. NewFile (.new) → OldFile
			if (!IFileManager::Get().Move(*Op.OldFile, *Op.NewFile, true))
			{
				UE_LOG(LogPPakPacher, Error,
					TEXT("ApplyPatch::ReplaceFilesAtomic - failed to apply NewFile: %s -> %s"),
					*Op.NewFile, *Op.OldFile);
				bAllOk = false;
				break;
			}
			Op.bMovedFromNew = true;
		}

		// 阶段 2：全成功 → 清理 .bak；任何失败 → 回滚
		if (bAllOk)
		{
			for (const FOp& Op : Ops)
			{
				if (Op.bMovedToBak)
				{
					IFileManager::Get().Delete(*Op.BakFile, false, true /*bEvenReadOnly*/, true /*bQuiet*/);
				}
				UE_LOG(LogPPakPacher, Display,
					TEXT("ApplyPatch::ReplaceFilesAtomic - Rename: %s -> %s"), *Op.NewFile, *Op.OldFile);
			}
			return true;
		}

		// 回滚已成功的 ops（逆序）
		UE_LOG(LogPPakPacher, Warning,
			TEXT("ApplyPatch::ReplaceFilesAtomic - rolling back %d ops..."), Ops.Num());
		for (int32 i = Ops.Num() - 1; i >= 0; --i)
		{
			const FOp& Op = Ops[i];
			if (Op.bMovedFromNew)
			{
				// 把已应用的 OldFile（实际是新版本）改回 .new 留作下次重试
				if (!IFileManager::Get().Move(*Op.NewFile, *Op.OldFile, true))
				{
					UE_LOG(LogPPakPacher, Error,
						TEXT("ApplyPatch::ReplaceFilesAtomic - rollback step1 failed: %s -> %s"), *Op.OldFile, *Op.NewFile);
				}
			}
			if (Op.bMovedToBak)
			{
				// 把 .bak 改回 OldFile（恢复原始）
				if (!IFileManager::Get().Move(*Op.OldFile, *Op.BakFile, true))
				{
					UE_LOG(LogPPakPacher, Error,
						TEXT("ApplyPatch::ReplaceFilesAtomic - rollback step2 failed: %s -> %s"), *Op.BakFile, *Op.OldFile);
				}
			}
		}
		return false;
	};

	// 并发执行所有 Modify chunk 的 PatchDiff + 替换文件
	FPPakPatcherPerfReport PatchSummary;
	if (ApplyTasks.Num() > 0)
	{
		const int32 ThreadNum = UPPakPatcherSettings::Get().PatchTaskThreadNum;
		FPPakPatcherTaskRunner Runner(ThreadNum, TEXT("ApplyPatch"));

		UE_LOG(LogPPakPacher, Display,
			TEXT("FPPatchManager::ApplyPatch - Dispatching %d Modify tasks."), ApplyTasks.Num());

		const int32 NumTasks = ApplyTasks.Num();
		TArray<bool> TaskResults;
		TaskResults.SetNumZeroed(NumTasks);

		// 进度追踪
		ProgressCurrent.store(0, std::memory_order_relaxed);
		ProgressTotal.store(NumTasks, std::memory_order_relaxed);
		std::atomic<int32>* ProgressPtr = &ProgressCurrent;

		for (int32 i = 0; i < NumTasks; ++i)
		{
			const FApplyTask& T = ApplyTasks[i];
			// 风格说明：当前 ApplyPatch 在 Runner.Wait() 之前不返回，引用捕获 InResDir/ReplaceFilesAtomic/TaskResults
			// 是安全的；但为了防御 fire-and-forget 模式重构，这里改为按值捕获（FString/lambda 拷贝成本可忽略），
			// 仅 PatchSummary 与 ProgressPtr 仍按引用 / 指针捕获——它们必须跨 task 累加。
			Runner.Submit([T, ReplaceFilesAtomic, &TaskResults, i, NumTasks, InResDir, ProgressPtr, &PatchSummary](int32 TaskIdx) -> bool
			{
				const double TaskStart = FPlatformTime::Seconds();
				UE_LOG(LogPPakPacher, Display,
					TEXT("ApplyPatch[Task %d/%d] - Start. Chunk=%s OldPak=%s NewPak=%s Patch=%s"),
					i + 1, NumTasks, *T.Item.ChunkName, *T.OldFullPath, *T.NewFullPath, *T.PatchFullPath);

				FPPakPatcherPerfReport LocalPerf;
				FPResPatcher LocalPatcher;
				FPResPatchDataPtr Patch = MakeShared<FPResPatchData, ESPMode::ThreadSafe>();
				if (!Patch->LoadFromFile(T.PatchFullPath))
				{
					UE_LOG(LogPPakPacher, Error,
						TEXT("ApplyPatch[Task %d/%d] - Patch load failed: %s"), i + 1, NumTasks, *T.PatchFullPath);
					TaskResults[i] = false;
					return false;
				}

				if (!LocalPatcher.PatchDiff(T.NewFullPath, T.OldFullPath, Patch, &LocalPerf))
				{
					UE_LOG(LogPPakPacher, Error,
						TEXT("ApplyPatch[Task %d/%d] - PatchDiff failed. Chunk=%s OldPak=%s NewPak=%s Patch=%s"),
						i + 1, NumTasks, *T.Item.ChunkName, *T.OldFullPath, *T.NewFullPath, *T.PatchFullPath);
					// 释放 LocalPatcher 持有的 old pak 句柄，避免后续 cleanup 失败
					LocalPatcher = FPResPatcher();
					Patch.Reset();

					// Cleanup .new
					IFileManager::Get().Delete(*T.NewFullPath, false);
					if (T.Item.Utoc.IsValid())
					{
						IFileManager::Get().Delete(*(InResDir / T.Item.Utoc.FileName + TEXT(".new")), false);
					}
					if (T.Item.Ucas.IsValid())
					{
						IFileManager::Get().Delete(*(InResDir / T.Item.Ucas.FileName + TEXT(".new")), false);
					}
					TaskResults[i] = false;
					return false;
				}

				// 释放文件句柄（rename 老 pak 之前必须先放掉 reader）
				Patch.Reset();
				LocalPatcher = FPResPatcher();

				// 文件替换（每个 chunk 操作的是不同文件，并发安全）。
				// 两阶段提交防半状态：pak/utoc/ucas 必须全成功或全回滚，
				// 阶段 1: rename OldFile → .bak；阶段 2: rename NewFile(.new) → OldFile；任一失败回滚 .bak。
				TArray<FPPatchManifestFileEntry> TargetEntries;
				TargetEntries.Reserve(3);
				TargetEntries.Add(T.Item.Pak);
				if (T.Item.Utoc.IsValid()) TargetEntries.Add(T.Item.Utoc);
				if (T.Item.Ucas.IsValid()) TargetEntries.Add(T.Item.Ucas);

				bool bAllOk = ReplaceFilesAtomic(InResDir, TargetEntries);

				const double Elapsed = FPlatformTime::Seconds() - TaskStart;
				if (!bAllOk)
				{
					UE_LOG(LogPPakPacher, Error,
						TEXT("ApplyPatch[Task %d/%d] - ReplaceFilesAtomic failed (rolled back). Chunk=%s Elapsed=%.2fs"),
						i + 1, NumTasks, *T.Item.ChunkName, Elapsed);
				}
				else
				{
					UE_LOG(LogPPakPacher, Display,
						TEXT("ApplyPatch[Task %d/%d] - Done. Chunk=%s Elapsed=%.2fs"),
						i + 1, NumTasks, *T.Item.ChunkName, Elapsed);
				}
				TaskResults[i] = bAllOk;
				PatchSummary.ThreadSafeMergeFrom(LocalPerf);
				ProgressPtr->fetch_add(1, std::memory_order_relaxed);
				return bAllOk;
			});
		}

		Runner.Wait();

		for (bool bOk : TaskResults)
		{
			if (bOk) ++NumModify;
			else     ++NumFailed;
		}
	}

	UE_LOG(LogPPakPacher, Display,
		TEXT("FPPatchManager::ApplyPatch - Done. Modify=%d Add=%d Equal=%d Delete=%d Failed=%d"),
		NumModify, NumAdd, NumEqual, NumDelete, NumFailed);

	// 保存 PerfReport 汇总（修复 #39）
	//
	// 文件名与 CreatePatch 端保持一致（perf_summary.json），通过目录区分两端：
	//   - CreatePatch: <PatchDir>/perf_summary.json   （patch 产出阶段）
	//   - ApplyPatch:  <PatchedRes>/perf_summary.json （patch 应用阶段）
	//
	// 旧路径 <PatchDir>/perf_patch_summary.json 已废弃，不再写入。
	// 这样语义清晰：PatchDir 装"构建产物 + 构建 perf"；PatchedRes 装"应用结果 + 应用 perf"。
	if (ApplyTasks.Num() > 0)
	{
		const FString SummaryPath = InResDir / TEXT("perf_summary.json");
		PatchSummary.SaveToFile(SummaryPath);
		UE_LOG(LogPPakPacher, Display,
			TEXT("ApplyPatch - PerfSummary saved (%d chunks): %s"), ApplyTasks.Num(), *SummaryPath);
	}

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
