#include "Patcher/FPResPatcher.h"
#include "PPakPatcherModule.h"
#include "Patcher/PPakPatcher.h"
#include "Utils/PPakPatcherUtils.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"

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
	EPPakPatchMode InMode, EPakPatchCompressType InCompressType)
{
	// 1. .utoc/.ucas 单独传入是不被支持的（应该传对应 .pak，IoStore 由 pak 同伴自动处理）
	if (IsIoStore(InNewFile))
	{
		UE_LOG(LogPPakPacher, Error,
			TEXT("FPResPatcher::CreateDiff - IoStore files (.utoc/.ucas) should not be passed individually; ")
			TEXT("pass the companion .pak instead. File: %s"), *InNewFile);
		return false;
	}

	// 2. Mode=Binary：不论扩展名一律走整体 HDiff（文档约定"与文件类型无关"）
	if (InMode == EPPakPatchMode::Binary)
	{
		if (IsPak(InNewFile))
		{
			UE_LOG(LogPPakPacher, Display,
				TEXT("FPResPatcher::CreateDiff - Mode=Binary on .pak: bypassing PakAware, doing whole-file HDiff. File: %s"), *InNewFile);
		}
		return CreateBinDiff(InPatchFilename, InNewFile, InOldFile, OutPatch, InCompressType);
	}

	// 3. Mode=PakAware + .pak：走 FPPakPatcher（含 IoStore 联动）
	if (IsPak(InNewFile))
	{
		FPPakPatcher* Patcher = GetOrCreatePakPatcher();
		if (!Patcher)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::CreateDiff - Failed to get PakPatcher. Patch:%s"), *InPatchFilename);
			return false;
		}
		return Patcher->CreateDiff(InPatchFilename, InNewFile, InOldFile, OutPatch, InMode, InCompressType);
	}

	// 4. Mode=PakAware + 非 .pak：Bin 分支
	return CreateBinDiff(InPatchFilename, InNewFile, InOldFile, OutPatch, InCompressType);
}

// -------------------------------------------------------------------------
// PatchDiff : 对旧文件打补丁，产出新文件
//   分发优先级：Header.Type → Pak/IoStore 走 FPPakPatcher；Bin 走 PatchBin。
//   即使 NewFile 是 .pak，只要 patch 是用 -Mode=Binary 生成的（Header.Type=Bin），
//   也走整体 HDiff 还原；与 CreateDiff 严格对称。
// -------------------------------------------------------------------------

bool FPResPatcher::PatchDiff(const FString& InNewFile, const FString& InOldFile, const FPResPatchDataPtr& InPatch)
{
	if (!InPatch.IsValid())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::PatchDiff - InPatch is invalid."));
		return false;
	}

	if (IsIoStore(InNewFile))
	{
		UE_LOG(LogPPakPacher, Error,
			TEXT("FPResPatcher::PatchDiff - IoStore files (.utoc/.ucas) should not be passed individually; ")
			TEXT("pass the companion .pak instead. File: %s"), *InNewFile);
		return false;
	}

	// 按 patch Header.Type 分发（与 CreateDiff 对称）：
	//   Bin → 整体 HDiff（即使 NewFile 是 .pak，只要 patch 用 -Mode=Binary 生成也按 Bin 还原）
	//   Pak → FPPakPatcher（含 IoStore 联动）
	if (InPatch->Header.Type == EPResPatchType::Bin)
	{
		return PatchBin(InNewFile, InOldFile, InPatch);
	}
	if (InPatch->Header.Type == EPResPatchType::Pak)
	{
		FPPakPatcher* Patcher = GetOrCreatePakPatcher();
		if (!Patcher)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::PatchDiff - Failed to get PakPatcher. NewFile:%s"), *InNewFile);
			return false;
		}
		return Patcher->PatchDiff(InNewFile, InOldFile, InPatch);
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

	if (IsIoStore(InNewFile))
	{
		UE_LOG(LogPPakPacher, Error,
			TEXT("FPResPatcher::CheckDiff - IoStore files (.utoc/.ucas) should not be passed individually; ")
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
	EPakPatchCompressType InCompressType)
{
	IPBinPatcher* Patcher = GetBinPatcher();
	if (!Patcher)
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::CreateBinDiff - Failed to get BinPatcher. Patch:%s"), *InPatchFilename);
		return false;
	}

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

	TArray<uint8> DiffData;
	if (!Patcher->CreateDiff(NewData, OldData, DiffData, InCompressType))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::CreateBinDiff - BinPatcher CreateDiff failed. NewFile:%s"), *InNewFile);
		return false;
	}

	// 初始化 FPResPatchData（Bin 类型）并把 HDiff 字节写入 DataBlock
	// 构建侧：始终同时计算 MD5 + CRC32
	// Header 中只存 clean filename（不存构建机绝对路径，避免泄露 + 运行侧无意义）
	OutPatch = MakeShared<FPResPatchData, ESPMode::ThreadSafe>();
	OutPatch->BeginRecord(InPatchFilename, EPResPatchType::Bin,
		FPaths::GetCleanFilename(InOldFile), FPaths::GetCleanFilename(InNewFile),
		FPPakPatcherUtils::CalculateFileMD5String(InOldFile), FPPakPatcherUtils::CalculateFileMD5String(InNewFile),
		FPPakPatcherUtils::CalculateFileCrc32(InOldFile),     FPPakPatcherUtils::CalculateFileCrc32(InNewFile),
		InCompressType);

	// Header 元数据：Bin 没有 pak 版本号，OldVersion/NewVersion 留 0；OldSize/NewSize 已知。
	// GenerateMode 默认为 Binary（Bin 分支语义自然就是整体 HDiff，无 PakAware 的概念）。
	OutPatch->Header.OldSize = OldData.Num();
	OutPatch->Header.NewSize = NewData.Num();
	OutPatch->Header.GenerateMode = EPPakPatchMode::Binary;

	check(OutPatch->GetBinBody() != nullptr);
	OutPatch->RecordDataBlock(
		OutPatch->GetBinBody()->DiffInfo,
		/*NewOffset*/ 0, /*NewSize*/ NewData.Num(),
		/*OldOffset*/ 0, /*OldSize*/ OldData.Num(),
		DiffData.GetData(), DiffData.Num(), /*bIsPatchData*/ true);

	OutPatch->EndRecord();

	if (!InPatchFilename.IsEmpty() && OutPatch->IsUsePrecache())
	{
		if (!OutPatch->SaveToFile(InPatchFilename))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::CreateBinDiff - Failed to save patch file: %s"), *InPatchFilename);
			return false;
		}
	}

	return true;
}

bool FPResPatcher::PatchBin(const FString& InNewFile, const FString& InOldFile, const FPResPatchDataPtr& InPatch)
{
	if (InPatch->Header.Type != EPResPatchType::Bin || InPatch->GetBinBody() == nullptr)
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::PatchBin - Patch type mismatch (expect Bin). NewFile:%s"), *InNewFile);
		return false;
	}

	IPBinPatcher* Patcher = GetBinPatcher();
	if (!Patcher)
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::PatchBin - Failed to get BinPatcher. NewFile:%s"), *InNewFile);
		return false;
	}

	TArray<uint8> OldData;
	if (!FPPakPatcherUtils::LoadFileToBuffer(InOldFile, OldData))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::PatchBin - Failed to load old file: %s"), *InOldFile);
		return false;
	}

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

	TArray<uint8> NewData;
	NewData.SetNumUninitialized(Body->DiffInfo.NewSize);
	if (!Patcher->Patch(NewData.GetData(), (uint64)NewData.Num(),
		OldData.GetData(), (uint64)OldData.Num(),
		DiffData.GetData(), (uint64)DiffData.Num()))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::PatchBin - BinPatcher Patch failed. NewFile:%s"), *InNewFile);
		return false;
	}

	if (!FPPakPatcherUtils::DumpMemoryToFile(InNewFile, NewData.GetData(), NewData.Num()))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatcher::PatchBin - Failed to write new file: %s"), *InNewFile);
		return false;
	}

	// 运行时：根据 CheckFileHashType 校验 patched new file
	if (!FPPakPatcherUtils::VerifyFileHashByCheckType(InNewFile,
		InPatch->Header.NewMD5, InPatch->Header.NewCrc32,
		TEXT("FPResPatcher::PatchBin/New")))
	{
		return false;
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
