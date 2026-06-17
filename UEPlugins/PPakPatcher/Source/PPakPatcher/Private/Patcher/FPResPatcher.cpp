#include "Patcher/FPResPatcher.h"
#include "PPakPatcherModule.h"
#include "PPakPatcherSettings.h"
#include "Patcher/PPakPatcher.h"
#include "Utils/PPakPatcherUtils.h"
#include "Utils/PPakPatcherPerfReport.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"

// -------------------------------------------------------------------------
// 构造 / 析构
// -------------------------------------------------------------------------

FPResPatcher::FPResPatcher()
{
}

FPResPatcher::~FPResPatcher()
{
}

// -------------------------------------------------------------------------
// 资源类型判定（静态）
// -------------------------------------------------------------------------

bool FPResPatcher::IsPak(const FString& InFilename)
{
	const FString Ext = FPaths::GetExtension(InFilename).ToLower();
	return Ext == TEXT("pak");
}

bool FPResPatcher::IsIoStore(const FString& InFilename)
{
	const FString Ext = FPaths::GetExtension(InFilename).ToLower();
	return Ext == TEXT("ucas") || Ext == TEXT("utoc");
}

// -------------------------------------------------------------------------
// 内部：PakPatcher 懒加载（per-task 状态）；BinPatcher 直接借用 Module 单实例
// -------------------------------------------------------------------------

FPPakPatcher* FPResPatcher::GetOrCreatePakPatcher()
{
	if (!PakPatcher.IsValid())
	{
		PakPatcher = IPPakPatcherModule::Get().CreatePakPatcher();
	}
	return PakPatcher.Get();
}

IPBinPatcher* FPResPatcher::GetBinPatcher()
{
	return IPPakPatcherModule::Get().GetBinPatcher();
}

// -------------------------------------------------------------------------
// CreateDiff : 统一差量生成入口
//   分发优先级：Mode=Binary → 不论扩展名都走整体 HDiff（Bin 分支）；
//               Mode=PakAware：按扩展名分发到 .pak / Bin。
//   .utoc/.ucas 单独传入始终拒绝（必须传对应 .pak，IoStore 由 pak 同伴自动处理）。
// -------------------------------------------------------------------------

bool FPResPatcher::CreateDiff(const FString& InPatchFilename,
	const FString& InNewFile, const FString& InOldFile,
	FPResPatchDataPtr& OutPatch,
	EPPakPatchMode InMode, EPakPatchCompressType InCompressType,
	FPPakPatcherPerfReport* OutPerfReport)
{
	UE_LOG(LogPPakPacher, Display,
		TEXT("FPResPatcher::CreateDiff - Begin. New:%s Old:%s Patch:%s Mode=%s CompressType=%s"),
		*InNewFile, *InOldFile, *InPatchFilename,
		*UEnum::GetValueAsString(InMode),
		*UEnum::GetValueAsString(InCompressType));

	// 1. .utoc/.ucas 单独传入：PakAware 模式下不允许（由 pak 同伴联动），Binary 模式下允许整体 diff
	if (IsIoStore(InNewFile) && InMode != EPPakPatchMode::Binary)
	{
		UE_LOG(LogPPakPacher, Error,
			TEXT("FPResPatcher::CreateDiff - IoStore files (.utoc/.ucas) should not be passed individually in PakAware mode; ")
			TEXT("pass the companion .pak instead. File: %s"), *InNewFile);
		return false;
	}

	// 2. Mode=Binary：不论扩展名一律走整体 HDiff（文档约定"与文件类型无关"）
	if (InMode == EPPakPatchMode::Binary)
	{
		UE_LOG(LogPPakPacher, Display,
			TEXT("FPResPatcher::CreateDiff - Dispatch -> Binary (whole-file HDiff). File:%s IsPak=%d IsIoStore=%d"),
			*InNewFile, IsPak(InNewFile) ? 1 : 0, IsIoStore(InNewFile) ? 1 : 0);
		return CreateBinDiff(InPatchFilename, InNewFile, InOldFile, OutPatch, InCompressType, OutPerfReport);
	}

	// 3. Mode=PakAware + .pak：走 FPPakPatcher（含 IoStore 联动）
	if (IsPak(InNewFile))
	{
		UE_LOG(LogPPakPacher, Display,
			TEXT("FPResPatcher::CreateDiff - Dispatch -> PakAware (FPPakPatcher). File:%s"), *InNewFile);
		FPPakPatcher* Patcher = GetOrCreatePakPatcher();
		if (!Patcher)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::CreateDiff - Failed to get PakPatcher. Patch:%s"), *InPatchFilename);
			return false;
		}
		return Patcher->CreateDiff(InPatchFilename, InNewFile, InOldFile, OutPatch, InMode, InCompressType, OutPerfReport);
	}

	// 4. Mode=PakAware + 非 .pak：Bin 分支
	UE_LOG(LogPPakPacher, Display,
		TEXT("FPResPatcher::CreateDiff - Dispatch -> Bin branch (non-pak file in PakAware mode). File:%s"), *InNewFile);
	return CreateBinDiff(InPatchFilename, InNewFile, InOldFile, OutPatch, InCompressType, OutPerfReport);
}

// -------------------------------------------------------------------------
// PatchDiff : 对旧文件打补丁，产出新文件
//   分发优先级：Header.Type → Pak/IoStore 走 FPPakPatcher；Bin 走 PatchBin。
//   即使 NewFile 是 .pak，只要 patch 是用 -Mode=Binary 生成的（Header.Type=Bin），
//   也走整体 HDiff 还原；与 CreateDiff 严格对称。
// -------------------------------------------------------------------------

bool FPResPatcher::PatchDiff(const FString& InNewFile, const FString& InOldFile, const FPResPatchDataPtr& InPatch,
	FPPakPatcherPerfReport* OutPerfReport /*= nullptr*/)
{
	if (!InPatch.IsValid())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::PatchDiff - InPatch is invalid. NewFile:%s OldFile:%s"), *InNewFile, *InOldFile);
		return false;
	}

	UE_LOG(LogPPakPacher, Display,
		TEXT("FPResPatcher::PatchDiff - Begin. New:%s Old:%s Header.Type=%d PatchMode=%s"),
		*InNewFile, *InOldFile, (int32)InPatch->Header.Type,
		*UEnum::GetValueAsString(InPatch->Header.PatchMode));

	if (IsIoStore(InNewFile) && InPatch->Header.Type != EPResPatchType::Bin)
	{
		UE_LOG(LogPPakPacher, Error,
			TEXT("FPResPatcher::PatchDiff - IoStore files (.utoc/.ucas) should not be passed individually in PakAware mode; ")
			TEXT("pass the companion .pak instead. File: %s"), *InNewFile);
		return false;
	}

	// 按 patch Header.Type 分发（与 CreateDiff 对称）：
	//   Bin → 整体 HDiff（即使 NewFile 是 .pak，只要 patch 用 -Mode=Binary 生成也按 Bin 还原）
	//   Pak → FPPakPatcher（含 IoStore 联动）
	if (InPatch->Header.Type == EPResPatchType::Bin)
	{
		UE_LOG(LogPPakPacher, Display, TEXT("FPResPatcher::PatchDiff - Dispatch -> Bin (PatchBin). File:%s"), *InNewFile);
		return PatchBin(InNewFile, InOldFile, InPatch, OutPerfReport);
	}
	if (InPatch->Header.Type == EPResPatchType::Pak)
	{
		UE_LOG(LogPPakPacher, Display, TEXT("FPResPatcher::PatchDiff - Dispatch -> Pak (FPPakPatcher::PatchDiff). File:%s"), *InNewFile);
		FPPakPatcher* Patcher = GetOrCreatePakPatcher();
		if (!Patcher)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::PatchDiff - Failed to get PakPatcher. NewFile:%s"), *InNewFile);
			return false;
		}
		return Patcher->PatchDiff(InNewFile, InOldFile, InPatch, OutPerfReport);
	}

	UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::PatchDiff - Unknown Header.Type=%d. NewFile:%s"),
		(int32)InPatch->Header.Type, *InNewFile);
	return false;
}

// -------------------------------------------------------------------------
// CheckDiff : 补丁数据回测校验（按 Header.Type 分发，与 PatchDiff 对称）
// -------------------------------------------------------------------------

bool FPResPatcher::CheckDiff(const FString& InNewFile, const FString& InOldFile, const FPResPatchDataPtr& InPatch)
{
	if (!InPatch.IsValid())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::CheckDiff - InPatch is invalid."));
		return false;
	}

	if (IsIoStore(InNewFile) && InPatch->Header.Type != EPResPatchType::Bin)
	{
		UE_LOG(LogPPakPacher, Error,
			TEXT("FPResPatcher::CheckDiff - IoStore files (.utoc/.ucas) should not be passed individually in PakAware mode; ")
			TEXT("pass the companion .pak instead. File: %s"), *InNewFile);
		return false;
	}

	if (IsPak(InNewFile) && InPatch->Header.Type == EPResPatchType::Pak)
	{
		FPPakPatcher* Patcher = GetOrCreatePakPatcher();
		if (!Patcher)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::CheckDiff - Failed to get PakPatcher. NewFile:%s"), *InNewFile);
			return false;
		}
		return Patcher->CheckDiff(InNewFile, InOldFile, InPatch);
	}

	// Bin 分支：包含 InPatch.Type=Bin（即使 NewFile 是 .pak，patch 也按 Bin 还原）
	return CheckBinDiff(InNewFile, InOldFile, InPatch);
}

// =========================================================================
// Bin 分支私有实现
// =========================================================================

bool FPResPatcher::CreateBinDiff(const FString& InPatchFilename,
	const FString& InNewFile, const FString& InOldFile,
	FPResPatchDataPtr& OutPatch,
	EPakPatchCompressType InCompressType,
	FPPakPatcherPerfReport* OutPerfReport /*= nullptr*/)
{
	UE_LOG(LogPPakPacher, Display,
		TEXT("FPResPatcher::CreateBinDiff - Begin. New:%s Old:%s Patch:%s CompressType=%s"),
		*InNewFile, *InOldFile, *InPatchFilename,
		*UEnum::GetValueAsString(InCompressType));

	FPPakPatcherPerfReport PerfReport;
	const double StartTime = FPPakPatcherPerfReport::Now();

	IPBinPatcher* Patcher = GetBinPatcher();
	if (!Patcher)
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::CreateBinDiff - Failed to get BinPatcher. Patch:%s"), *InPatchFilename);
		return false;
	}

	// 统计 .pak + .utoc + .ucas 总大小
	auto CalcGroupSize = [](const FString& PakFilename) -> int64
	{
		int64 Total = FPPakPatcherUtils::GetFileSize(PakFilename);
		const FString Base = FPaths::ChangeExtension(PakFilename, TEXT(""));
		int64 S = FPPakPatcherUtils::GetFileSize(Base + TEXT(".utoc"));
		if (S > 0) Total += S;
		S = FPPakPatcherUtils::GetFileSize(Base + TEXT(".ucas"));
		if (S > 0) Total += S;
		return Total;
	};
	PerfReport.TotalOldAssetSize = CalcGroupSize(InOldFile);
	PerfReport.TotalNewAssetSize = CalcGroupSize(InNewFile);

	PPATCHER_PERF_BEGIN(&PerfReport, TimeRead);
	TArray<uint8> NewData, OldData;
	if (!FPPakPatcherUtils::LoadFileToBuffer(InNewFile, NewData))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::CreateBinDiff - Failed to load new file: %s"), *InNewFile);
		return false;
	}
	if (!FPPakPatcherUtils::LoadFileToBuffer(InOldFile, OldData))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::CreateBinDiff - Failed to load old file: %s"), *InOldFile);
		return false;
	}
	PPATCHER_PERF_END(&PerfReport, TimeRead);

	PPATCHER_PERF_BEGIN(&PerfReport, TimeDiff);
	TArray<uint8> DiffData;
	if (!Patcher->CreateDiff(NewData, OldData, DiffData, InCompressType))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::CreateBinDiff - BinPatcher CreateDiff failed. NewFile:%s"), *InNewFile);
		return false;
	}
	PPATCHER_PERF_END(&PerfReport, TimeDiff);
	PerfReport.PakEntryDiffSize += DiffData.Num();

	OutPatch = MakeShared<FPResPatchData, ESPMode::ThreadSafe>();
	OutPatch->BeginRecord(InPatchFilename, EPResPatchType::Bin,
		FPaths::GetCleanFilename(InOldFile), FPaths::GetCleanFilename(InNewFile),
		FPPakPatcherUtils::CalculateFileMD5String(InOldFile), FPPakPatcherUtils::CalculateFileMD5String(InNewFile),
		FPPakPatcherUtils::CalculateFileCrc32(InOldFile),     FPPakPatcherUtils::CalculateFileCrc32(InNewFile),
		InCompressType,
		UPPakPatcherSettings::Get().ExternalCompressType,
		(int8)UPPakPatcherSettings::Get().ExternalCompressLevel);

	OutPatch->Header.OldSize = OldData.Num();
	OutPatch->Header.NewSize = NewData.Num();
	OutPatch->Header.PatchMode = EPPakPatchMode::Binary;

	check(OutPatch->GetBinBody() != nullptr);
	OutPatch->RecordDataBlock(
		OutPatch->GetBinBody()->DiffInfo,
		/*NewOffset*/ 0, /*NewSize*/ NewData.Num(),
		/*OldOffset*/ 0, /*OldSize*/ OldData.Num(),
		DiffData.GetData(), DiffData.Num(), /*bIsPatchData*/ true);

	// IoStore 同伴联动
	if (IsPak(InNewFile))
	{
		FPBinPatchBody* Body = OutPatch->GetBinBody();
		const FString NewBase = FPaths::ChangeExtension(InNewFile, TEXT(""));
		const FString OldBase = FPaths::ChangeExtension(InOldFile, TEXT(""));

		auto DiffIoCompanion = [&](const FString& InExt, bool& bOutHasDiff, FPPakPatchDataInfo& OutInfo,
			int64& OutOldSize, int64& OutNewSize) -> bool
		{
			const FString NewIo = NewBase + TEXT(".") + InExt;
			const FString OldIo = OldBase + TEXT(".") + InExt;
			if (!IFileManager::Get().FileExists(*NewIo) || !IFileManager::Get().FileExists(*OldIo))
				return true;

			PPATCHER_PERF_BEGIN(&PerfReport, TimeRead);
			TArray<uint8> NewIoData, OldIoData;
			FPPakPatcherUtils::LoadFileToBuffer(NewIo, NewIoData);
			FPPakPatcherUtils::LoadFileToBuffer(OldIo, OldIoData);
			PPATCHER_PERF_END(&PerfReport, TimeRead);

			if (NewIoData == OldIoData) return true;

			PPATCHER_PERF_BEGIN(&PerfReport, TimeDiff);
			TArray<uint8> IoDiff;
			if (!Patcher->CreateDiff(NewIoData, OldIoData, IoDiff, InCompressType))
			{
				UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::CreateBinDiff - %s diff failed"), *InExt);
				return false;
			}
			PPATCHER_PERF_END(&PerfReport, TimeDiff);
			PerfReport.IoStoreDiffSize += IoDiff.Num();

			bOutHasDiff = true;
			OutOldSize = OldIoData.Num();
			OutNewSize = NewIoData.Num();
			OutPatch->RecordDataBlock(OutInfo,
				0, NewIoData.Num(), 0, OldIoData.Num(),
				IoDiff.GetData(), IoDiff.Num(), true);
			return true;
		};

		if (!DiffIoCompanion(TEXT("utoc"), Body->bHasUtocDiff, Body->UtocDiffInfo, Body->UtocOldSize, Body->UtocNewSize))
			return false;
		if (!DiffIoCompanion(TEXT("ucas"), Body->bHasUcasDiff, Body->UcasDiffInfo, Body->UcasOldSize, Body->UcasNewSize))
			return false;
	}

	OutPatch->EndRecord();

	PPATCHER_PERF_BEGIN(&PerfReport, TimeWrite);
	if (!InPatchFilename.IsEmpty() && OutPatch->IsUsePrecache())
	{
		if (!OutPatch->SaveToFile(InPatchFilename))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::CreateBinDiff - Failed to save patch file: %s"), *InPatchFilename);
			return false;
		}
	}
	PPATCHER_PERF_END(&PerfReport, TimeWrite);

	PerfReport.TimeTotal = FPPakPatcherPerfReport::Since(StartTime);
	PerfReport.LogSummary(TEXT("CreateBinDiff"));

	if (OutPerfReport)
	{
		OutPerfReport->MergeFrom(PerfReport);
	}

	return true;
}

bool FPResPatcher::PatchBin(const FString& InNewFile, const FString& InOldFile, const FPResPatchDataPtr& InPatch,
	FPPakPatcherPerfReport* OutPerfReport /*= nullptr*/)
{
	UE_LOG(LogPPakPacher, Display,
		TEXT("FPResPatcher::PatchBin - Begin. New:%s Old:%s"), *InNewFile, *InOldFile);

	FPPakPatcherPerfReport PerfReport;
	const double StartTime = FPPakPatcherPerfReport::Now();

	if (InPatch->Header.Type != EPResPatchType::Bin || InPatch->GetBinBody() == nullptr)
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::PatchBin - Patch type mismatch (expect Bin). NewFile:%s Header.Type=%d HasBody=%d"),
			*InNewFile, (int32)InPatch->Header.Type, InPatch->GetBinBody() ? 1 : 0);
		return false;
	}

	IPBinPatcher* Patcher = GetBinPatcher();
	if (!Patcher)
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::PatchBin - Failed to get BinPatcher. NewFile:%s"), *InNewFile);
		return false;
	}

	PPATCHER_PERF_BEGIN(&PerfReport, TimeRead);
	TArray<uint8> OldData;
	if (!FPPakPatcherUtils::LoadFileToBuffer(InOldFile, OldData))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::PatchBin - Failed to load old file: %s"), *InOldFile);
		return false;
	}
	PPATCHER_PERF_END(&PerfReport, TimeRead);

	// 运行时：根据 CheckFileHashType 校验 old file
	if (!FPPakPatcherUtils::VerifyFileHashByCheckType(InOldFile,
		InPatch->Header.OldMD5, InPatch->Header.OldCrc32,
		TEXT("FPResPatcher::PatchBin/Old")))
	{
		return false;
	}

	FPBinPatchBody* Body = InPatch->GetBinBody();
	TArray<uint8> DiffData;
	if (!InPatch->GetFilePatchData(Body->DiffInfo, DiffData))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::PatchBin - Failed to read diff data from patch. NewFile:%s"), *InNewFile);
		return false;
	}

	PPATCHER_PERF_BEGIN(&PerfReport, TimePatch);
	TArray<uint8> NewData;
	NewData.SetNumUninitialized(Body->DiffInfo.NewSize);
	if (!Patcher->Patch(NewData.GetData(), (uint64)NewData.Num(),
		OldData.GetData(), (uint64)OldData.Num(),
		DiffData.GetData(), (uint64)DiffData.Num()))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::PatchBin - BinPatcher Patch failed. NewFile:%s"), *InNewFile);
		return false;
	}
	PPATCHER_PERF_END(&PerfReport, TimePatch);

	PPATCHER_PERF_BEGIN(&PerfReport, TimeWrite);
	if (!FPPakPatcherUtils::DumpMemoryToFile(InNewFile, NewData.GetData(), NewData.Num()))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::PatchBin - Failed to write new file: %s"), *InNewFile);
		return false;
	}
	PPATCHER_PERF_END(&PerfReport, TimeWrite);

	// 运行时：根据 CheckFileHashType 校验 patched new file
	if (!FPPakPatcherUtils::VerifyFileHashByCheckType(InNewFile,
		InPatch->Header.NewMD5, InPatch->Header.NewCrc32,
		TEXT("FPResPatcher::PatchBin/New")))
	{
		return false;
	}

	// IoStore 同伴联动
	auto PatchIoStoreCompanion = [&](bool bHasDiff, FPPakPatchDataInfo& IoInfo, int64 IoOldSize, int64 IoNewSize,
		const TCHAR* Ext) -> bool
	{
		if (!bHasDiff) return true;
		const FString OldBase = FPaths::ChangeExtension(InOldFile, TEXT(""));
		const FString IoOldPath = OldBase + TEXT(".") + Ext;
		const FString IoOutputPath = IoOldPath + TEXT(".new");

		PPATCHER_PERF_BEGIN(&PerfReport, TimeRead);
		TArray<uint8> IoOldData;
		if (!FPPakPatcherUtils::LoadFileToBuffer(IoOldPath, IoOldData))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::PatchBin - Failed to load IoStore old: %s"), *IoOldPath);
			return false;
		}
		PPATCHER_PERF_END(&PerfReport, TimeRead);

		TArray<uint8> IoDiffData;
		if (!InPatch->GetFilePatchData(IoInfo, IoDiffData))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::PatchBin - Failed to read IoStore diff: %s"), Ext);
			return false;
		}

		PPATCHER_PERF_BEGIN(&PerfReport, TimePatch);
		TArray<uint8> IoNewData;
		IoNewData.SetNumUninitialized(IoNewSize);
		if (!Patcher->Patch(IoNewData.GetData(), (uint64)IoNewData.Num(),
			IoOldData.GetData(), (uint64)IoOldData.Num(),
			IoDiffData.GetData(), (uint64)IoDiffData.Num()))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::PatchBin - IoStore Patch failed: %s"), *IoOldPath);
			return false;
		}
		PPATCHER_PERF_END(&PerfReport, TimePatch);

		PPATCHER_PERF_BEGIN(&PerfReport, TimeWrite);
		if (!FPPakPatcherUtils::DumpMemoryToFile(IoOutputPath, IoNewData.GetData(), IoNewData.Num()))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::PatchBin - Failed to write IoStore new: %s"), *IoOutputPath);
			return false;
		}
		PPATCHER_PERF_END(&PerfReport, TimeWrite);
		UE_LOG(LogPPakPacher, Display, TEXT("FPResPatcher::PatchBin - IoStore patched: %s"), *IoOutputPath);
		return true;
	};

	if (!PatchIoStoreCompanion(Body->bHasUtocDiff, Body->UtocDiffInfo, Body->UtocOldSize, Body->UtocNewSize, TEXT("utoc")))
		return false;
	if (!PatchIoStoreCompanion(Body->bHasUcasDiff, Body->UcasDiffInfo, Body->UcasOldSize, Body->UcasNewSize, TEXT("ucas")))
		return false;

	PerfReport.TimeTotal = FPPakPatcherPerfReport::Since(StartTime);
	PerfReport.LogSummary(TEXT("PatchBin"));

	if (OutPerfReport)
	{
		OutPerfReport->MergeFrom(PerfReport);
	}

	return true;
}

bool FPResPatcher::CheckBinDiff(const FString& InNewFile, const FString& InOldFile, const FPResPatchDataPtr& InPatch)
{
	if (InPatch->Header.Type != EPResPatchType::Bin || InPatch->GetBinBody() == nullptr)
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::CheckBinDiff - Patch type mismatch (expect Bin). NewFile:%s"), *InNewFile);
		return false;
	}

	IPBinPatcher* Patcher = GetBinPatcher();
	if (!Patcher)
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::CheckBinDiff - Failed to get BinPatcher. NewFile:%s"), *InNewFile);
		return false;
	}

	TArray<uint8> NewData, OldData;
	if (!FPPakPatcherUtils::LoadFileToBuffer(InNewFile, NewData))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::CheckBinDiff - Failed to load new file: %s"), *InNewFile);
		return false;
	}
	if (!FPPakPatcherUtils::LoadFileToBuffer(InOldFile, OldData))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::CheckBinDiff - Failed to load old file: %s"), *InOldFile);
		return false;
	}

	FPBinPatchBody* Body = InPatch->GetBinBody();
	TArray<uint8> DiffData;
	if (!InPatch->GetFilePatchData(Body->DiffInfo, DiffData))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::CheckBinDiff - Failed to read diff data from patch. NewFile:%s"), *InNewFile);
		return false;
	}

	return Patcher->CheckDiff(NewData, OldData, DiffData);
}
