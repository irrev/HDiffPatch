#include "Patcher/PIoStorePatcher.h"
#include "PPakPatcherModule.h"
#include "Data/PPakPatcherDataType.h"
#include "Utils/PPakPatcherUtils.h"
#include "PPakPatcherSettings.h"

#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"

namespace
{
	/**
	 * 从 .pak 路径推导 IoStore 同伴路径。
	 * 支持 .new 后缀：如果输入是 "A.pak.new"，先剥掉 ".new"，
	 * 推导出 "A.utoc"，再追加 ".new" → "A.utoc.new"。
	 * 这保证所有 .new 产物命名统一为"原名.new"。
	 */
	FString DeriveCompanionPath(const FString& InPakFile, const TCHAR* InCompanionExt)
	{
		FString Path = InPakFile;
		bool bHasNewSuffix = false;
		if (FPaths::GetExtension(Path).Equals(TEXT("new"), ESearchCase::IgnoreCase))
		{
			bHasNewSuffix = true;
			Path = FPaths::GetBaseFilename(Path, false);  // 剥掉 .new → "A.pak"
		}
		Path = FPaths::ChangeExtension(Path, InCompanionExt);  // "A.pak" → "A.utoc"
		if (bHasNewSuffix)
		{
			Path += TEXT(".new");  // "A.utoc" → "A.utoc.new"
		}
		return Path;
	}

	FString DeriveUtocPath(const FString& InPakFile) { return DeriveCompanionPath(InPakFile, TEXT("utoc")); }
	FString DeriveUcasPath(const FString& InPakFile) { return DeriveCompanionPath(InPakFile, TEXT("ucas")); }

	/**
	 * 产出 NewFile 相对 OldFile 的 HDiff 字节流，并填入 OutDiffInfo + 写入 InOutPatch.Data。
	 * 若 OldFile 不存在或为空 → 走 full data 路径（bIsPatchData=false）。
	 */
	bool RecordFileDiff(IPBinPatcher* BinPatcher, FPResPatchData& InOutPatch,
		const FString& InNewFile, const FString& InOldFile,
		FPPakPatchDataInfo& OutDiffInfo)
	{
		TArray<uint8> NewData;
		if (!FPPakPatcherUtils::LoadFileToBuffer(InNewFile, NewData))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPIoStorePatcher::RecordFileDiff - Failed to load new file: %s"), *InNewFile);
			return false;
		}

		TArray<uint8> OldData;
		const bool bOldExist = !InOldFile.IsEmpty() && IFileManager::Get().FileExists(*InOldFile)
			&& FPPakPatcherUtils::LoadFileToBuffer(InOldFile, OldData) && OldData.Num() > 0;

		if (bOldExist)
		{
			TArray<uint8> DiffData;
			if (!BinPatcher->CreateDiff(NewData.GetData(), NewData.Num(), OldData.GetData(), OldData.Num(), DiffData))
			{
				UE_LOG(LogPPakPacher, Error, TEXT("FPIoStorePatcher::RecordFileDiff - BinPatcher CreateDiff failed. NewFile:%s"), *InNewFile);
				return false;
			}
			const bool bCheckOk = BinPatcher->CheckDiff(NewData.GetData(), NewData.Num(), OldData.GetData(), OldData.Num(), DiffData.GetData(), DiffData.Num());
			check(bCheckOk);

			InOutPatch.RecordDataBlock(OutDiffInfo,
				/*NewOffset*/ 0, /*NewSize*/ NewData.Num(),
				/*OldOffset*/ 0, /*OldSize*/ OldData.Num(),
				DiffData.GetData(), DiffData.Num(), /*bIsPatchData*/ true);
			return true;
		}
		else
		{
			// full data
			InOutPatch.RecordDataBlock(OutDiffInfo,
				/*NewOffset*/ 0, /*NewSize*/ NewData.Num(),
				/*OldOffset*/ 0, /*OldSize*/ 0,
				NewData.GetData(), NewData.Num(), /*bIsPatchData*/ false);
			return true;
		}
	}

	/**
	 * 用 DiffInfo 描述的 HDiff 字节，把 OldFile 重建为 NewFile 写盘。
	 */
	bool ApplyFileDiff(IPBinPatcher* BinPatcher, const FPResPatchData& InPatch,
		FPPakPatchDataInfo& InDiffInfo, const FString& InNewFile, const FString& InOldFile)
	{
		TArray<uint8> DiffData;
		if (!const_cast<FPResPatchData&>(InPatch).GetFilePatchData(InDiffInfo, DiffData))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPIoStorePatcher::ApplyFileDiff - Read diff data failed. NewFile:%s"), *InNewFile);
			return false;
		}

		TArray<uint8> NewData;
		NewData.SetNumUninitialized(InDiffInfo.NewSize);

		if (InDiffInfo.bIsPatchData)
		{
			TArray<uint8> OldData;
			if (!FPPakPatcherUtils::LoadFileToBuffer(InOldFile, OldData))
			{
				UE_LOG(LogPPakPacher, Error, TEXT("FPIoStorePatcher::ApplyFileDiff - Load old file failed: %s"), *InOldFile);
				return false;
			}
			if (!BinPatcher->Patch(NewData.GetData(), (uint64)NewData.Num(),
				OldData.GetData(), (uint64)OldData.Num(),
				DiffData.GetData(), (uint64)DiffData.Num()))
			{
				UE_LOG(LogPPakPacher, Error, TEXT("FPIoStorePatcher::ApplyFileDiff - Patch failed. NewFile:%s"), *InNewFile);
				return false;
			}
		}
		else
		{
			// full data：直接复制
			check(DiffData.Num() == InDiffInfo.NewSize);
			FMemory::Memcpy(NewData.GetData(), DiffData.GetData(), InDiffInfo.NewSize);
		}

		if (!FPPakPatcherUtils::DumpMemoryToFile(InNewFile, NewData.GetData(), NewData.Num()))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPIoStorePatcher::ApplyFileDiff - Write new file failed: %s"), *InNewFile);
			return false;
		}
		return true;
	}

	/**
	 * 校验：把回放后的字节与磁盘上的 NewFile 做字节级 verify；同时按 CheckFileHashType 校验 NewFile hash。
	 */
	bool VerifyFileDiff(IPBinPatcher* BinPatcher, const FPResPatchData& InPatch,
		FPPakPatchDataInfo& InDiffInfo, const FString& InNewFile, const FString& InOldFile,
		const FString& InExpectedNewMD5, uint32 InExpectedNewCrc32,
		const TCHAR* InContextTagForLog)
	{
		// new file hash 校验（按 CheckFileHashType 选择性）
		if (!FPPakPatcherUtils::VerifyFileHashByCheckType(InNewFile,
			InExpectedNewMD5, InExpectedNewCrc32, InContextTagForLog))
		{
			return false;
		}

		// 字节级 verify：用 patch 在 old 上回放，与 new 对比
		TArray<uint8> NewDiskData;
		if (!FPPakPatcherUtils::LoadFileToBuffer(InNewFile, NewDiskData))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPIoStorePatcher::VerifyFileDiff - Load new file failed: %s"), *InNewFile);
			return false;
		}

		TArray<uint8> DiffData;
		if (!const_cast<FPResPatchData&>(InPatch).GetFilePatchData(InDiffInfo, DiffData))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPIoStorePatcher::VerifyFileDiff - Read diff data failed. NewFile:%s"), *InNewFile);
			return false;
		}

		if (InDiffInfo.bIsPatchData)
		{
			TArray<uint8> OldData;
			if (!FPPakPatcherUtils::LoadFileToBuffer(InOldFile, OldData))
			{
				UE_LOG(LogPPakPacher, Error, TEXT("FPIoStorePatcher::VerifyFileDiff - Load old file failed: %s"), *InOldFile);
				return false;
			}
			return BinPatcher->CheckDiff(NewDiskData.GetData(), NewDiskData.Num(),
				OldData.GetData(), OldData.Num(),
				DiffData.GetData(), DiffData.Num());
		}
		else
		{
			// full data: 直接字节比对
			if (DiffData.Num() != NewDiskData.Num()
				|| FMemory::Memcmp(DiffData.GetData(), NewDiskData.GetData(), DiffData.Num()) != 0)
			{
				UE_LOG(LogPPakPacher, Error, TEXT("FPIoStorePatcher::VerifyFileDiff - Full data mismatch. File:%s"), *InNewFile);
				return false;
			}
			return true;
		}
	}
}

// ---------------------------------------------------------------------------

bool FPIoStorePatcher::HasIoStoreSibling(const FString& InPakFile)
{
	const FString Utoc = DeriveUtocPath(InPakFile);
	const FString Ucas = DeriveUcasPath(InPakFile);
	return IFileManager::Get().FileExists(*Utoc) || IFileManager::Get().FileExists(*Ucas);
}

bool FPIoStorePatcher::CreateIoStoreDiff(const FString& InPakNewFile, const FString& InPakOldFile, FPResPatchData& InOutPatch)
{
	const FString NewUtoc = DeriveUtocPath(InPakNewFile);
	const FString NewUcas = DeriveUcasPath(InPakNewFile);
	const FString OldUtoc = DeriveUtocPath(InPakOldFile);
	const FString OldUcas = DeriveUcasPath(InPakOldFile);

	const bool bNewUtocExist = IFileManager::Get().FileExists(*NewUtoc);
	const bool bNewUcasExist = IFileManager::Get().FileExists(*NewUcas);
	if (!bNewUtocExist && !bNewUcasExist)
	{
		// 没有 IoStore 同伴，无需处理
		return true;
	}

	IPBinPatcher* BinPatcher = IPPakPatcherModule::Get().GetBinPatcher();
	if (!BinPatcher)
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPIoStorePatcher::CreateIoStoreDiff - Failed to get BinPatcher."));
		return false;
	}

	// 确保 IoStoreBody 存在（若上层是 Pak 类型则手动 new 一个）
	if (InOutPatch.IoStoreBody.IsValid() == false)
	{
		InOutPatch.IoStoreBody = MakeUnique<FPIoStorePatchBody>();
	}
	FPIoStorePatchBody* Body = InOutPatch.IoStoreBody.Get();
	Body->Strategy = EIoStoreDiffStrategy::FileBinary;

	// .utoc
	if (bNewUtocExist)
	{
		// 构建侧：始终同时计算 MD5 + Crc32
		Body->OldUtocMD5   = FPPakPatcherUtils::CalculateFileMD5String(OldUtoc);
		Body->NewUtocMD5   = FPPakPatcherUtils::CalculateFileMD5String(NewUtoc);
		Body->OldUtocCrc32 = FPPakPatcherUtils::CalculateFileCrc32(OldUtoc);
		Body->NewUtocCrc32 = FPPakPatcherUtils::CalculateFileCrc32(NewUtoc);
		if (!RecordFileDiff(BinPatcher, InOutPatch, NewUtoc, OldUtoc, Body->UtocDiffInfo))
		{
			return false;
		}
	}

	// .ucas
	if (bNewUcasExist)
	{
		Body->OldUcasMD5   = FPPakPatcherUtils::CalculateFileMD5String(OldUcas);
		Body->NewUcasMD5   = FPPakPatcherUtils::CalculateFileMD5String(NewUcas);
		Body->OldUcasCrc32 = FPPakPatcherUtils::CalculateFileCrc32(OldUcas);
		Body->NewUcasCrc32 = FPPakPatcherUtils::CalculateFileCrc32(NewUcas);
		if (!RecordFileDiff(BinPatcher, InOutPatch, NewUcas, OldUcas, Body->UcasDiffInfo))
		{
			return false;
		}
	}

	UE_LOG(LogPPakPacher, Display, TEXT("FPIoStorePatcher::CreateIoStoreDiff - Done. utoc=%d ucas=%d"), bNewUtocExist ? 1 : 0, bNewUcasExist ? 1 : 0);
	return true;
}

bool FPIoStorePatcher::PatchIoStore(const FString& InPakNewFile, const FString& InPakOldFile, const FPResPatchData& InPatch)
{
	const FPIoStorePatchBody* Body = InPatch.GetIoStoreBody();
	if (Body == nullptr)
	{
		// patch 里没有 IoStore body，跳过
		return true;
	}

	IPBinPatcher* BinPatcher = IPPakPatcherModule::Get().GetBinPatcher();
	if (!BinPatcher)
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPIoStorePatcher::PatchIoStore - Failed to get BinPatcher."));
		return false;
	}

	const FString NewUtoc = DeriveUtocPath(InPakNewFile);
	const FString NewUcas = DeriveUcasPath(InPakNewFile);
	const FString OldUtoc = DeriveUtocPath(InPakOldFile);
	const FString OldUcas = DeriveUcasPath(InPakOldFile);

	// .utoc
	if (Body->UtocDiffInfo.NewSize > 0)
	{
		// old 文件 hash 校验（运行时按 CheckFileHashType 选择 None/Crc32/MD5）
		if (Body->UtocDiffInfo.bIsPatchData)
		{
			if (!FPPakPatcherUtils::VerifyFileHashByCheckType(OldUtoc,
				Body->OldUtocMD5, Body->OldUtocCrc32,
				TEXT("FPIoStorePatcher::PatchIoStore/OldUtoc")))
			{
				return false;
			}
		}
		if (!ApplyFileDiff(BinPatcher, InPatch, const_cast<FPPakPatchDataInfo&>(Body->UtocDiffInfo), NewUtoc, OldUtoc))
		{
			return false;
		}
	}

	// .ucas
	if (Body->UcasDiffInfo.NewSize > 0)
	{
		if (Body->UcasDiffInfo.bIsPatchData)
		{
			if (!FPPakPatcherUtils::VerifyFileHashByCheckType(OldUcas,
				Body->OldUcasMD5, Body->OldUcasCrc32,
				TEXT("FPIoStorePatcher::PatchIoStore/OldUcas")))
			{
				return false;
			}
		}
		if (!ApplyFileDiff(BinPatcher, InPatch, const_cast<FPPakPatchDataInfo&>(Body->UcasDiffInfo), NewUcas, OldUcas))
		{
			return false;
		}
	}

	return true;
}

bool FPIoStorePatcher::CheckIoStoreDiff(const FString& InPakNewFile, const FString& InPakOldFile, const FPResPatchData& InPatch)
{
	const FPIoStorePatchBody* Body = InPatch.GetIoStoreBody();
	if (Body == nullptr)
	{
		return true;
	}

	IPBinPatcher* BinPatcher = IPPakPatcherModule::Get().GetBinPatcher();
	if (!BinPatcher)
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPIoStorePatcher::CheckIoStoreDiff - Failed to get BinPatcher."));
		return false;
	}

	const FString NewUtoc = DeriveUtocPath(InPakNewFile);
	const FString NewUcas = DeriveUcasPath(InPakNewFile);
	const FString OldUtoc = DeriveUtocPath(InPakOldFile);
	const FString OldUcas = DeriveUcasPath(InPakOldFile);

	if (Body->UtocDiffInfo.NewSize > 0)
	{
		if (!VerifyFileDiff(BinPatcher, InPatch, const_cast<FPPakPatchDataInfo&>(Body->UtocDiffInfo),
			NewUtoc, OldUtoc,
			Body->NewUtocMD5, Body->NewUtocCrc32,
			TEXT("FPIoStorePatcher::CheckIoStoreDiff/Utoc")))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPIoStorePatcher::CheckIoStoreDiff - .utoc verify failed."));
			return false;
		}
	}

	if (Body->UcasDiffInfo.NewSize > 0)
	{
		if (!VerifyFileDiff(BinPatcher, InPatch, const_cast<FPPakPatchDataInfo&>(Body->UcasDiffInfo),
			NewUcas, OldUcas,
			Body->NewUcasMD5, Body->NewUcasCrc32,
			TEXT("FPIoStorePatcher::CheckIoStoreDiff/Ucas")))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPIoStorePatcher::CheckIoStoreDiff - .ucas verify failed."));
			return false;
		}
	}

	return true;
}
