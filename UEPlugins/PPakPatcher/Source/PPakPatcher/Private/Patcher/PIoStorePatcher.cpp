#include "Patcher/PIoStorePatcher.h"
#include "PPakPatcherModule.h"
#include "Data/PPakPatcherDataType.h"
#include "Data/PPakPatcherKeyChainHelper.h"
#include "Utils/PPakPatcherUtils.h"
#include "Utils/PPakPatcherPerfReport.h"
#include "PPakPatcherSettings.h"

#include "Misc/AES.h"
#include "Misc/Compression.h"
#include "Misc/KeyChainUtilities.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "IO/IoStore.h"

namespace
{
	FString DeriveCompanionPath(const FString& InPakFile, const TCHAR* InCompanionExt)
	{
		FString Path = InPakFile;
		bool bHasNewSuffix = false;
		if (FPaths::GetExtension(Path).Equals(TEXT("new"), ESearchCase::IgnoreCase))
		{
			bHasNewSuffix = true;
			Path = FPaths::GetBaseFilename(Path, false);
		}
		Path = FPaths::ChangeExtension(Path, InCompanionExt);
		if (bHasNewSuffix)
		{
			Path += TEXT(".new");
		}
		return Path;
	}

	FString DeriveUtocPath(const FString& InPakFile) { return DeriveCompanionPath(InPakFile, TEXT("utoc")); }
	FString DeriveUcasPath(const FString& InPakFile) { return DeriveCompanionPath(InPakFile, TEXT("ucas")); }

	/**
	 * FileBinary helper: 产出 NewFile 相对 OldFile 的 HDiff 字节流。
	 */
	bool RecordFileDiff(IPBinPatcher* BinPatcher, FPResPatchData& InOutPatch,
		const FString& InNewFile, const FString& InOldFile,
		FPPakPatchDataInfo& OutDiffInfo,
		EPakPatchCompressType InCompressType = EPakPatchCompressType::None)
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
			if (!BinPatcher->CreateDiff(NewData.GetData(), NewData.Num(), OldData.GetData(), OldData.Num(), DiffData, InCompressType))
			{
				UE_LOG(LogPPakPacher, Error, TEXT("FPIoStorePatcher::RecordFileDiff - BinPatcher CreateDiff failed. NewFile:%s CompressType=%s"),
					*InNewFile, *UEnum::GetValueAsString(InCompressType));
				return false;
			}
			const bool bCheckOk = BinPatcher->CheckDiff(NewData.GetData(), NewData.Num(), OldData.GetData(), OldData.Num(), DiffData.GetData(), DiffData.Num());
			check(bCheckOk);

			InOutPatch.RecordDataBlock(OutDiffInfo,
				0, NewData.Num(), 0, OldData.Num(),
				DiffData.GetData(), DiffData.Num(), true);
			return true;
		}
		else
		{
			InOutPatch.RecordDataBlock(OutDiffInfo,
				0, NewData.Num(), 0, 0,
				NewData.GetData(), NewData.Num(), false);
			return true;
		}
	}

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

	bool VerifyFileDiff(IPBinPatcher* BinPatcher, const FPResPatchData& InPatch,
		FPPakPatchDataInfo& InDiffInfo, const FString& InNewFile, const FString& InOldFile,
		const FString& InExpectedNewMD5, uint32 InExpectedNewCrc32,
		const TCHAR* InContextTagForLog)
	{
		if (!FPPakPatcherUtils::VerifyFileHashByCheckType(InNewFile,
			InExpectedNewMD5, InExpectedNewCrc32, InContextTagForLog))
		{
			return false;
		}

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
			if (DiffData.Num() != NewDiskData.Num()
				|| FMemory::Memcmp(DiffData.GetData(), NewDiskData.GetData(), DiffData.Num()) != 0)
			{
				UE_LOG(LogPPakPacher, Error, TEXT("FPIoStorePatcher::VerifyFileDiff - Full data mismatch. File:%s"), *InNewFile);
				return false;
			}
			return true;
		}
	}

	/**
	 * 从 ucas 文件中读取指定范围的原始字节。
	 */
	bool ReadUcasRange(const FString& InUcasFile, int64 InOffset, int64 InLength, TArray<uint8>& OutData)
	{
		TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*InUcasFile));
		if (!Reader) return false;
		const int64 FileSize = Reader->TotalSize();
		if (InOffset + InLength > FileSize)
		{
			UE_LOG(LogPPakPacher, Error,
				TEXT("ReadUcasRange - Offset+Length (%lld+%lld=%lld) exceeds file size (%lld). File:%s"),
				InOffset, InLength, InOffset + InLength, FileSize, *InUcasFile);
			return false;
		}
		Reader->Seek(InOffset);
		OutData.SetNumUninitialized(InLength);
		Reader->Serialize(OutData.GetData(), InLength);
		return !Reader->IsError();
	}
}

// ---------------------------------------------------------------------------

bool FPIoStorePatcher::HasIoStoreSibling(const FString& InPakFile)
{
	const FString Utoc = DeriveUtocPath(InPakFile);
	const FString Ucas = DeriveUcasPath(InPakFile);
	return IFileManager::Get().FileExists(*Utoc) || IFileManager::Get().FileExists(*Ucas);
}

// ---------------------------------------------------------------------------
// CreateIoStoreDiff
// ---------------------------------------------------------------------------

bool FPIoStorePatcher::CreateIoStoreDiff(const FString& InPakNewFile, const FString& InPakOldFile, FPResPatchData& InOutPatch,
	int64* OutModifyPhysicalSize,
	FPPakPatcherPerfReport* InOutPerfReport,
	EPakPatchCompressType InCompressType)
{
	const FString NewUtoc = DeriveUtocPath(InPakNewFile);
	const FString NewUcas = DeriveUcasPath(InPakNewFile);
	const FString OldUtoc = DeriveUtocPath(InPakOldFile);
	const FString OldUcas = DeriveUcasPath(InPakOldFile);

	const bool bNewUtocExist = IFileManager::Get().FileExists(*NewUtoc);
	const bool bNewUcasExist = IFileManager::Get().FileExists(*NewUcas);
	if (!bNewUtocExist && !bNewUcasExist)
	{
		UE_LOG(LogPPakPacher, Display,
			TEXT("FPIoStorePatcher::CreateIoStoreDiff - No IoStore companion found for %s, skip."), *InPakNewFile);
		return true;
	}

	UE_LOG(LogPPakPacher, Display,
		TEXT("FPIoStorePatcher::CreateIoStoreDiff - Begin. NewPak:%s OldPak:%s NewUtocExist=%d NewUcasExist=%d CompressType=%s"),
		*InPakNewFile, *InPakOldFile, bNewUtocExist ? 1 : 0, bNewUcasExist ? 1 : 0,
		*UEnum::GetValueAsString(InCompressType));

	IPBinPatcher* BinPatcher = IPPakPatcherModule::Get().GetBinPatcher();
	if (!BinPatcher)
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPIoStorePatcher::CreateIoStoreDiff - Failed to get BinPatcher."));
		return false;
	}

	if (InOutPatch.IoStoreBody.IsValid() == false)
	{
		InOutPatch.IoStoreBody = MakeUnique<FPIoStorePatchBody>();
	}
	FPIoStorePatchBody* Body = InOutPatch.IoStoreBody.Get();

	// .utoc hash（两种策略都需要）
	if (bNewUtocExist)
	{
		Body->OldUtocMD5   = FPPakPatcherUtils::CalculateFileMD5String(OldUtoc);
		Body->NewUtocMD5   = FPPakPatcherUtils::CalculateFileMD5String(NewUtoc);
		Body->OldUtocCrc32 = FPPakPatcherUtils::CalculateFileCrc32(OldUtoc);
		Body->NewUtocCrc32 = FPPakPatcherUtils::CalculateFileCrc32(NewUtoc);
	}

	// .ucas hash
	if (bNewUcasExist)
	{
		Body->OldUcasMD5   = FPPakPatcherUtils::CalculateFileMD5String(OldUcas);
		Body->NewUcasMD5   = FPPakPatcherUtils::CalculateFileMD5String(NewUcas);
		Body->OldUcasCrc32 = FPPakPatcherUtils::CalculateFileCrc32(OldUcas);
		Body->NewUcasCrc32 = FPPakPatcherUtils::CalculateFileCrc32(NewUcas);
	}

	// 决定策略：如果有 utoc 且可解析，走 ChunkAware；否则 fallback 到 FileBinary
	const bool bOldUtocExist = IFileManager::Get().FileExists(*OldUtoc);
	const bool bOldUcasExist = IFileManager::Get().FileExists(*OldUcas);
	bool bUseChunkAware = bNewUtocExist && bNewUcasExist && bOldUtocExist && bOldUcasExist;

	if (bUseChunkAware)
	{
		// 解析新旧 .utoc
		FIoStoreTocResource NewToc, OldToc;
		FIoStatus NewStatus = FIoStoreTocResource::Read(*NewUtoc, EIoStoreTocReadOptions::Default, NewToc);
		FIoStatus OldStatus = FIoStoreTocResource::Read(*OldUtoc, EIoStoreTocReadOptions::Default, OldToc);

		if (!NewStatus.IsOk() || !OldStatus.IsOk())
		{
			UE_LOG(LogPPakPacher, Warning,
				TEXT("FPIoStorePatcher::CreateIoStoreDiff - Failed to parse .utoc, falling back to FileBinary. New:%s Old:%s"),
				*NewStatus.ToString(), *OldStatus.ToString());
			bUseChunkAware = false;
		}

		// 验证 CompressionBlocks 的物理 offset 不越界
		if (bUseChunkAware)
		{
			const int64 NewUcasFileSize = IFileManager::Get().FileSize(*NewUcas);
			const int64 OldUcasFileSize = IFileManager::Get().FileSize(*OldUcas);
			bool bOffsetsValid = true;

			for (int32 i = 0; i < NewToc.CompressionBlocks.Num() && bOffsetsValid; ++i)
			{
				const int64 Off = (int64)NewToc.CompressionBlocks[i].GetOffset();
				const int64 Size = (int64)Align(NewToc.CompressionBlocks[i].GetCompressedSize(), FAES::AESBlockSize);
				if (Off + Size > NewUcasFileSize) bOffsetsValid = false;
			}
			for (int32 i = 0; i < OldToc.CompressionBlocks.Num() && bOffsetsValid; ++i)
			{
				const int64 Off = (int64)OldToc.CompressionBlocks[i].GetOffset();
				const int64 Size = (int64)Align(OldToc.CompressionBlocks[i].GetCompressedSize(), FAES::AESBlockSize);
				if (Off + Size > OldUcasFileSize) bOffsetsValid = false;
			}

			if (!bOffsetsValid)
			{
				UE_LOG(LogPPakPacher, Warning,
					TEXT("FPIoStorePatcher::CreateIoStoreDiff - CompressionBlock offset exceeds ucas file size, falling back to FileBinary."));
				bUseChunkAware = false;
			}
		}

		if (bUseChunkAware)
		{
			Body->Strategy = EIoStoreDiffStrategy::ChunkAware;

			// .utoc 整体 diff（体积小）
			{
				PPATCHER_PERF_BEGIN(InOutPerfReport, TimeDiff);
				const bool bRecOk = RecordFileDiff(BinPatcher, InOutPatch, NewUtoc, OldUtoc, Body->UtocDiffInfo, InCompressType);
				PPATCHER_PERF_END(InOutPerfReport, TimeDiff);
				if (!bRecOk)
				{
					return false;
				}
			}

			const uint32 NewBlockSize = NewToc.Header.CompressionBlockSize;
			const uint32 OldBlockSize = OldToc.Header.CompressionBlockSize;

			// IoStore ChunkAware 模式：与 PakAware 保持一致（从 Settings 读取统一的 PatchMode）
			const EPPakPatchMode PatchMode = UPPakPatcherSettings::Get().PakPatchMode;
			const bool bDecrypt    = PPakPatchModeHelper::ShouldDecrypt(PatchMode);
			const bool bDecompress = PPakPatchModeHelper::ShouldDecompress(PatchMode);

			// 加密 key（仅 Decrypt 模式需要）
			const FAES::FAESKey* EncKey = nullptr;
			const bool bNewEncrypted = EnumHasAnyFlags(NewToc.Header.ContainerFlags, EIoContainerFlags::Encrypted);
			const bool bOldEncrypted = EnumHasAnyFlags(OldToc.Header.ContainerFlags, EIoContainerFlags::Encrypted);
			if (bDecrypt && (bNewEncrypted || bOldEncrypted))
			{
				FKeyChain& KeyChain = FPPakPatcherKeyChainHelper::Get().GetKeyChain();
				const FGuid& KeyGuid = NewToc.Header.EncryptionKeyGuid;
				if (KeyGuid.IsValid())
				{
					const FNamedAESKey* Found = KeyChain.GetEncryptionKeys().Find(KeyGuid);
					EncKey = Found ? &Found->Key : nullptr;
				}
				else if (const FNamedAESKey* Principal = KeyChain.GetPrincipalEncryptionKey())
				{
					EncKey = &Principal->Key;
				}
				if (!EncKey)
				{
					UE_LOG(LogPPakPacher, Error,
						TEXT("FPIoStorePatcher::CreateIoStoreDiff - ChunkAware requires encryption key but none found. Guid=%s"),
						*KeyGuid.ToString());
					return false;
				}
			}

			// 修复 #N1：Create 端 OldUcas/NewUcas Reader 复用
			//   - Apply 端已在 commit `ce9bf4c` 实现 OldUcasReader 复用；这里对称做 Create 端。
			//   - 旧实现：ReadChunkData 内部每次 `IFileManager::Get().CreateFileReader(*InUcasFile)`，
			//             N 个 chunk × 2（New/Old）= 2N 次 open/close 系统调用 + buffer 分配。
			//   - 新实现：主循环外预创建 NewUcasReader / OldUcasReader，ReadChunkData 接收 FArchive* 参数。
			TUniquePtr<FArchive> NewUcasReader(IFileManager::Get().CreateFileReader(*NewUcas));
			TUniquePtr<FArchive> OldUcasReader(IFileManager::Get().CreateFileReader(*OldUcas));
			if (!NewUcasReader || !OldUcasReader)
			{
				UE_LOG(LogPPakPacher, Error,
					TEXT("FPIoStorePatcher::CreateIoStoreDiff - Failed to open ucas readers. New:%s Old:%s"),
					*NewUcas, *OldUcas);
				return false;
			}
			const int64 NewUcasReaderSize = NewUcasReader->TotalSize();
			const int64 OldUcasReaderSize = OldUcasReader->TotalSize();

			/**
			 * Helper: 读取 chunk 覆盖的压缩块物理数据，可选解密/解压。
			 * - NoDecrypt: 返回原始物理字节（密文+压缩态）
			 * - DecryptAndCompress: 就地解密每个块后返回（压缩态明文）
			 * - DecryptAndDecompress: 解密+解压，拼出原始 uncompressed 数据
			 */
			auto ReadChunkData = [&](FArchive* InReader, int64 InReaderTotalSize,
				const FIoOffsetAndLength& InOffLen,
				const TArray<FIoStoreTocCompressedBlockEntry>& InBlocks,
				const TArray<FName>& InCompressionMethods,
				uint32 InCompressionBlockSize,
				bool bIsEncrypted,
				TArray<uint8>& OutData) -> bool
			{
				const int64 VirtualOffset = (int64)InOffLen.GetOffset();
				const int64 VirtualLength = (int64)InOffLen.GetLength();
				if (VirtualLength == 0) { OutData.Reset(); return true; }

				const int32 FirstBlockIndex = (int32)(VirtualOffset / InCompressionBlockSize);
				const int32 LastBlockIndex  = (int32)((Align(VirtualOffset + VirtualLength, (int64)InCompressionBlockSize) - 1) / InCompressionBlockSize);

				if (FirstBlockIndex >= InBlocks.Num() || LastBlockIndex >= InBlocks.Num())
				{
					return false;
				}

				// 读取物理字节
				const int64 PhysicalStart = (int64)InBlocks[FirstBlockIndex].GetOffset();
				int64 PhysicalEnd = (int64)InBlocks[LastBlockIndex].GetOffset() +
					Align((int64)InBlocks[LastBlockIndex].GetCompressedSize(), (int64)FAES::AESBlockSize);
				const int64 PhysicalSize = PhysicalEnd - PhysicalStart;

				PPATCHER_PERF_BEGIN(InOutPerfReport, TimeRead);
				if (!InReader) return false;
				if (PhysicalStart + PhysicalSize > InReaderTotalSize) return false;

				InReader->Seek(PhysicalStart);
				TArray<uint8> RawBuffer;
				RawBuffer.SetNumUninitialized(PhysicalSize);
				InReader->Serialize(RawBuffer.GetData(), PhysicalSize);
				const bool bReadErr = InReader->IsError();
				PPATCHER_PERF_END(InOutPerfReport, TimeRead);
				if (bReadErr) return false;

				if (!bDecrypt && !bDecompress)
				{
					// NoDecrypt：原样返回
					OutData = MoveTemp(RawBuffer);
					return true;
				}

				// DecryptAndCompress 或 DecryptAndDecompress：逐块解密
				if (bDecrypt && bIsEncrypted && EncKey)
				{
					PPATCHER_PERF_BEGIN(InOutPerfReport, TimeDecrypt);
					for (int32 Bi = FirstBlockIndex; Bi <= LastBlockIndex; ++Bi)
					{
						const int64 BlockPhysicalOffset = (int64)InBlocks[Bi].GetOffset() - PhysicalStart;
						const uint32 RawSize = Align(InBlocks[Bi].GetCompressedSize(), (uint32)FAES::AESBlockSize);
						FAES::DecryptData(RawBuffer.GetData() + BlockPhysicalOffset, RawSize, *EncKey);
					}
					PPATCHER_PERF_END(InOutPerfReport, TimeDecrypt);
				}

				if (!bDecompress)
				{
					// DecryptAndCompress：返回解密后的压缩数据
					OutData = MoveTemp(RawBuffer);
					return true;
				}

				// DecryptAndDecompress：逐块解压，拼出 uncompressed 数据
				PPATCHER_PERF_BEGIN(InOutPerfReport, TimeDecompress);
				OutData.SetNumUninitialized(VirtualLength);
				int64 DstOffset = 0;
				for (int32 Bi = FirstBlockIndex; Bi <= LastBlockIndex; ++Bi)
				{
					const int64 BlockPhysicalOffset = (int64)InBlocks[Bi].GetOffset() - PhysicalStart;
					const uint32 CompressedSize = InBlocks[Bi].GetCompressedSize();
					const uint32 UncompressedSize = InBlocks[Bi].GetUncompressedSize();
					const uint8 MethodIndex = InBlocks[Bi].GetCompressionMethodIndex();

					const int64 WriteSize = FMath::Min<int64>((int64)UncompressedSize, VirtualLength - DstOffset);
					if (WriteSize <= 0) break;

					if (MethodIndex == 0 || InCompressionMethods.Num() == 0)
					{
						// 无压缩：直接拷贝
						FMemory::Memcpy(OutData.GetData() + DstOffset, RawBuffer.GetData() + BlockPhysicalOffset, WriteSize);
					}
					else
					{
						const FName MethodName = (MethodIndex < (uint8)InCompressionMethods.Num()) ?
							InCompressionMethods[MethodIndex] : NAME_None;
						if (!FCompression::UncompressMemory(MethodName,
							OutData.GetData() + DstOffset, (int32)WriteSize,
							RawBuffer.GetData() + BlockPhysicalOffset, (int32)CompressedSize))
						{
							UE_LOG(LogPPakPacher, Error,
								TEXT("ReadChunkData - Decompress block %d failed. Method=%s CompSize=%d UncompSize=%d"),
								Bi, *MethodName.ToString(), CompressedSize, UncompressedSize);
							PPATCHER_PERF_END(InOutPerfReport, TimeDecompress);
							return false;
						}
					}
					DstOffset += WriteSize;
				}
				PPATCHER_PERF_END(InOutPerfReport, TimeDecompress);
				return true;
			};

			// 构建 Old ChunkId → Index 映射
			TMap<FIoChunkId, int32> OldChunkMap;
			for (int32 i = 0; i < OldToc.ChunkIds.Num(); ++i)
			{
				OldChunkMap.Add(OldToc.ChunkIds[i], i);
			}

			int32 NumKeep = 0, NumModify = 0, NumNew = 0, NumDelete = 0;

			// 遍历 New chunks
			for (int32 i = 0; i < NewToc.ChunkIds.Num(); ++i)
			{
				const FIoChunkId& NewChunkId = NewToc.ChunkIds[i];
				const FIoOffsetAndLength& NewOffLen = NewToc.ChunkOffsetLengths[i];
				const int64 NewVirtualOffset = (int64)NewOffLen.GetOffset();
				const int64 NewVirtualLength = (int64)NewOffLen.GetLength();

				FPIoStoreChunkPatchInfo ChunkInfo;
				ChunkInfo.ChunkId   = NewChunkId;
				ChunkInfo.NewOffset = NewVirtualOffset;
				ChunkInfo.NewLength = NewVirtualLength;

				const int32* OldIndexPtr = OldChunkMap.Find(NewChunkId);
				if (OldIndexPtr == nullptr)
				{
					// New chunk：full data（物理数据，不做预处理）
					ChunkInfo.PatchType = EIoStoreChunkPatchType::New;
					TArray<uint8> NewData;
					if (!ReadChunkData(NewUcasReader.Get(), NewUcasReaderSize, NewOffLen, NewToc.CompressionBlocks, NewToc.CompressionMethods, NewBlockSize, bNewEncrypted, NewData))
					{
						UE_LOG(LogPPakPacher, Error, TEXT("CreateIoStoreDiff - Read new chunk data failed. Chunk=%d"), i);
						return false;
					}
					InOutPatch.RecordDataBlock(ChunkInfo.DataInfo,
						NewVirtualOffset, NewData.Num(), 0, 0,
						NewData.GetData(), NewData.Num(), false);
					++NumNew;
				}
				else
				{
					const int32 OldIdx = *OldIndexPtr;
					const FIoOffsetAndLength& OldOffLen = OldToc.ChunkOffsetLengths[OldIdx];
					const int64 OldVirtualOffset = (int64)OldOffLen.GetOffset();
					const int64 OldVirtualLength = (int64)OldOffLen.GetLength();
					ChunkInfo.OldOffset = OldVirtualOffset;
					ChunkInfo.OldLength = OldVirtualLength;

					// 读取新旧数据（经过预处理）
					TArray<uint8> NewData, OldData;
					if (!ReadChunkData(NewUcasReader.Get(), NewUcasReaderSize, NewOffLen, NewToc.CompressionBlocks, NewToc.CompressionMethods, NewBlockSize, bNewEncrypted, NewData) ||
						!ReadChunkData(OldUcasReader.Get(), OldUcasReaderSize, OldOffLen, OldToc.CompressionBlocks, OldToc.CompressionMethods, OldBlockSize, bOldEncrypted, OldData))
					{
						UE_LOG(LogPPakPacher, Error, TEXT("CreateIoStoreDiff - Read chunk data failed. Chunk=%d"), i);
						return false;
					}

					if (NewData.Num() == OldData.Num() && FMemory::Memcmp(NewData.GetData(), OldData.GetData(), NewData.Num()) == 0)
					{
						ChunkInfo.PatchType = EIoStoreChunkPatchType::Keep;
						++NumKeep;
					}
					else
					{
						ChunkInfo.PatchType = EIoStoreChunkPatchType::Modify;
						TArray<uint8> DiffData;
						PPATCHER_PERF_BEGIN(InOutPerfReport, TimeDiff);
						const bool bDiffOk = BinPatcher->CreateDiff(NewData.GetData(), NewData.Num(), OldData.GetData(), OldData.Num(), DiffData, InCompressType);
						PPATCHER_PERF_END(InOutPerfReport, TimeDiff);
						if (!bDiffOk)
						{
							UE_LOG(LogPPakPacher, Error, TEXT("CreateIoStoreDiff - Chunk HDiff failed. Chunk=%d"), i);
							return false;
						}
						InOutPatch.RecordDataBlock(ChunkInfo.DataInfo,
							NewVirtualOffset, NewData.Num(), OldVirtualOffset, OldData.Num(),
							DiffData.GetData(), DiffData.Num(), true);
						++NumModify;

						// 累加 Modify chunk 在 new ucas 中的物理字节（用 NewToc 的 CompressionBlocks 计算）
						if (OutModifyPhysicalSize)
						{
							const int32 FirstBlk = (int32)(NewVirtualOffset / NewBlockSize);
							const int32 LastBlk = (int32)((Align(NewVirtualOffset + NewVirtualLength, (int64)NewBlockSize) - 1) / NewBlockSize);
							if (FirstBlk < NewToc.CompressionBlocks.Num() && LastBlk < NewToc.CompressionBlocks.Num())
							{
								const int64 PhysStart = (int64)NewToc.CompressionBlocks[FirstBlk].GetOffset();
								const int64 PhysEnd = (int64)NewToc.CompressionBlocks[LastBlk].GetOffset() +
									Align((int64)NewToc.CompressionBlocks[LastBlk].GetCompressedSize(), (int64)FAES::AESBlockSize);
								*OutModifyPhysicalSize += (PhysEnd - PhysStart);
							}
						}
					}

					OldChunkMap.Remove(NewChunkId);
				}

				Body->ChunkPatchInfos.Add(MoveTemp(ChunkInfo));
			}

			// 剩余的 Old chunks → Delete
			for (const auto& KV : OldChunkMap)
			{
				FPIoStoreChunkPatchInfo ChunkInfo;
				ChunkInfo.ChunkId = KV.Key;
				ChunkInfo.PatchType = EIoStoreChunkPatchType::Delete;
				const int32 OldIdx = KV.Value;
				ChunkInfo.OldOffset = (int64)OldToc.ChunkOffsetLengths[OldIdx].GetOffset();
				ChunkInfo.OldLength = (int64)OldToc.ChunkOffsetLengths[OldIdx].GetLength();
				Body->ChunkPatchInfos.Add(MoveTemp(ChunkInfo));
				++NumDelete;
			}

			UE_LOG(LogPPakPacher, Display,
				TEXT("FPIoStorePatcher::CreateIoStoreDiff - ChunkAware done. Chunks: Keep=%d Modify=%d New=%d Delete=%d Total=%d"),
				NumKeep, NumModify, NumNew, NumDelete, NewToc.ChunkIds.Num());

			// 记录 ucas 文件大小（含尾部 padding）
			Body->OldUcasFileSize = IFileManager::Get().FileSize(*OldUcas);
			Body->NewUcasFileSize = IFileManager::Get().FileSize(*NewUcas);

			// 尾部 padding：CompressionBlocks 覆盖范围之外的末尾数据
			// 计算 CompressionBlocks 覆盖的最大物理位置
			int64 NewBlocksCoveredEnd = 0;
			for (int32 i = 0; i < NewToc.CompressionBlocks.Num(); ++i)
			{
				int64 End = (int64)NewToc.CompressionBlocks[i].GetOffset() +
					Align((int64)NewToc.CompressionBlocks[i].GetCompressedSize(), (int64)FAES::AESBlockSize);
				NewBlocksCoveredEnd = FMath::Max(NewBlocksCoveredEnd, End);
			}
			if (Body->NewUcasFileSize > NewBlocksCoveredEnd)
			{
				// 有尾部数据（通常是 DirectoryIndex 或 padding）
				const int64 TailSize = Body->NewUcasFileSize - NewBlocksCoveredEnd;
				TArray<uint8> TailData;
				if (ReadUcasRange(NewUcas, NewBlocksCoveredEnd, TailSize, TailData))
				{
					// 用一个特殊 ChunkPatchInfo 记录尾部（PatchType=New，ChunkId 全零）
					FPIoStoreChunkPatchInfo TailInfo;
					FMemory::Memzero(&TailInfo.ChunkId, sizeof(FIoChunkId));
					TailInfo.PatchType = EIoStoreChunkPatchType::New;
					TailInfo.NewOffset = NewBlocksCoveredEnd; // 用作写入位置标记
					TailInfo.NewLength = TailSize;
					InOutPatch.RecordDataBlock(TailInfo.DataInfo,
						NewBlocksCoveredEnd, TailSize, 0, 0,
						TailData.GetData(), TailData.Num(), false);
					Body->ChunkPatchInfos.Add(MoveTemp(TailInfo));
				}
			}

			return true;
		}
	}

	// ═══ FileBinary fallback ═══
	Body->Strategy = EIoStoreDiffStrategy::FileBinary;

	if (bNewUtocExist)
	{
		PPATCHER_PERF_BEGIN(InOutPerfReport, TimeDiff);
		const bool bRecOk = RecordFileDiff(BinPatcher, InOutPatch, NewUtoc, OldUtoc, Body->UtocDiffInfo, InCompressType);
		PPATCHER_PERF_END(InOutPerfReport, TimeDiff);
		if (!bRecOk)
		{
			return false;
		}
	}

	if (bNewUcasExist)
	{
		PPATCHER_PERF_BEGIN(InOutPerfReport, TimeDiff);
		const bool bRecOk = RecordFileDiff(BinPatcher, InOutPatch, NewUcas, OldUcas, Body->UcasDiffInfo, InCompressType);
		PPATCHER_PERF_END(InOutPerfReport, TimeDiff);
		if (!bRecOk)
		{
			return false;
		}
	}

	UE_LOG(LogPPakPacher, Display, TEXT("FPIoStorePatcher::CreateIoStoreDiff - FileBinary done. utoc=%d ucas=%d"), bNewUtocExist ? 1 : 0, bNewUcasExist ? 1 : 0);
	return true;
}

// ---------------------------------------------------------------------------
// PatchIoStore
// ---------------------------------------------------------------------------

bool FPIoStorePatcher::PatchIoStore(const FString& InPakNewFile, const FString& InPakOldFile, const FPResPatchData& InPatch,
	FPPakPatcherPerfReport* InOutPerfReport)
{
	const FPIoStorePatchBody* Body = InPatch.GetIoStoreBody();
	if (Body == nullptr)
	{
		UE_LOG(LogPPakPacher, Display,
			TEXT("FPIoStorePatcher::PatchIoStore - No IoStoreBody in patch, skip. NewPak:%s"), *InPakNewFile);
		return true;
	}

	UE_LOG(LogPPakPacher, Display,
		TEXT("FPIoStorePatcher::PatchIoStore - Begin. NewPak:%s OldPak:%s Strategy=%s ChunkInfos=%d"),
		*InPakNewFile, *InPakOldFile,
		Body->Strategy == EIoStoreDiffStrategy::ChunkAware ? TEXT("ChunkAware") : TEXT("FileBinary"),
		Body->ChunkPatchInfos.Num());

	// PerfReport 计时辅助：如果传入了 PerfReport，则启动计时；否则空操作

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

	// .utoc：两种策略都走整体 diff 还原
	if (Body->UtocDiffInfo.NewSize > 0)
	{
		if (Body->UtocDiffInfo.bIsPatchData)
		{
			if (!FPPakPatcherUtils::VerifyFileHashByCheckType(OldUtoc,
				Body->OldUtocMD5, Body->OldUtocCrc32,
				TEXT("FPIoStorePatcher::PatchIoStore/OldUtoc")))
			{
				return false;
			}
		}
		PPATCHER_PERF_BEGIN(InOutPerfReport, TimePatch);
		if (!ApplyFileDiff(BinPatcher, InPatch, const_cast<FPPakPatchDataInfo&>(Body->UtocDiffInfo), NewUtoc, OldUtoc))
		{
			return false;
		}
		PPATCHER_PERF_END(InOutPerfReport, TimePatch); // utoc Patch 阶段（含读、Patch、写）整体记到 TimePatch
	}

	if (Body->Strategy == EIoStoreDiffStrategy::ChunkAware)
	{
		// .ucas：逐 chunk 还原
		if (!FPPakPatcherUtils::VerifyFileHashByCheckType(OldUcas,
			Body->OldUcasMD5, Body->OldUcasCrc32,
			TEXT("FPIoStorePatcher::PatchIoStore/OldUcas")))
		{
			return false;
		}

		// 解析新旧 .utoc（新 utoc 已还原）获取 CompressionBlocks
		FIoStoreTocResource NewToc, OldToc;
		FIoStatus NewStatus = FIoStoreTocResource::Read(*NewUtoc, EIoStoreTocReadOptions::Default, NewToc);
		FIoStatus OldStatus = FIoStoreTocResource::Read(*OldUtoc, EIoStoreTocReadOptions::Default, OldToc);
		if (!NewStatus.IsOk() || !OldStatus.IsOk())
		{
			UE_LOG(LogPPakPacher, Error, TEXT("PatchIoStore - Failed to parse utoc for ChunkAware. New:%s Old:%s"),
				*NewStatus.ToString(), *OldStatus.ToString());
			return false;
		}

		const uint32 NewBlockSize = NewToc.Header.CompressionBlockSize;
		const uint32 OldBlockSize = OldToc.Header.CompressionBlockSize;

		// Patch 端模式：从 patch header 读取（与 Create 侧一致）
		const EPPakPatchMode PatchMode = InPatch.Header.PatchMode;
		const bool bDecrypt    = PPakPatchModeHelper::ShouldDecrypt(PatchMode);
		const bool bDecompress = PPakPatchModeHelper::ShouldDecompress(PatchMode);

		const bool bOldEncrypted = EnumHasAnyFlags(OldToc.Header.ContainerFlags, EIoContainerFlags::Encrypted);
		const bool bNewEncrypted = EnumHasAnyFlags(NewToc.Header.ContainerFlags, EIoContainerFlags::Encrypted);

		const FAES::FAESKey* EncKey = nullptr;
		if (bDecrypt && (bOldEncrypted || bNewEncrypted))
		{
			FKeyChain& KeyChain = FPPakPatcherKeyChainHelper::Get().GetKeyChain();
			const FGuid& KeyGuid = NewToc.Header.EncryptionKeyGuid;
			if (KeyGuid.IsValid())
			{
				const FNamedAESKey* Found = KeyChain.GetEncryptionKeys().Find(KeyGuid);
				EncKey = Found ? &Found->Key : nullptr;
			}
			else if (const FNamedAESKey* Principal = KeyChain.GetPrincipalEncryptionKey())
			{
				EncKey = &Principal->Key;
			}
			if (!EncKey)
			{
				UE_LOG(LogPPakPacher, Error,
					TEXT("PatchIoStore - ChunkAware requires encryption key but none found. Guid=%s"),
					*KeyGuid.ToString());
				return false;
			}
		}

		// 使用记录的 new ucas 文件大小
		const int64 NewUcasPhysicalSize = Body->NewUcasFileSize;

		// 流式写：直接打开磁盘文件，边打边写（不再全量分配 new ucas buffer）
		TUniquePtr<FArchive> UcasWriter(IFileManager::Get().CreateFileWriter(*NewUcas));
		if (!UcasWriter)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("PatchIoStore - Failed to create ucas writer: %s"), *NewUcas);
			return false;
		}
		int64 LastWriteEnd = 0; // 用于断言 WriteOffset 单调递增

		// 共享 64KB 零缓冲：N 个 chunk gap 共用，避免每次 SetNumZeroed 大块分配。
		TArray<uint8> ZeroPaddingBuf;
		ZeroPaddingBuf.SetNumZeroed(64 * 1024);
		auto WriteGap = [&](int64 GapStart, int64 GapSize)
		{
			if (GapSize <= 0) return;
			UcasWriter->Seek(GapStart);
			int64 Remaining = GapSize;
			while (Remaining > 0)
			{
				const int64 ChunkSize = FMath::Min<int64>(Remaining, ZeroPaddingBuf.Num());
				UcasWriter->Serialize(ZeroPaddingBuf.GetData(), ChunkSize);
				Remaining -= ChunkSize;
			}
		};

		// ChunkAware 主循环外预创建 OldUcasReader 一次，主循环内 Seek+Serialize 复用。
		TUniquePtr<FArchive> OldUcasReader(IFileManager::Get().CreateFileReader(*OldUcas));
		if (!OldUcasReader)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("PatchIoStore - Failed to open OldUcas reader: %s"), *OldUcas);
			return false;
		}
		const int64 OldUcasFileSizeForReader = OldUcasReader->TotalSize();

		// 复用版本的 ReadChunkPhysical：使用上面预创建的 OldUcasReader（避免重复打开）
		auto ReadChunkPhysicalReused = [&](const FIoOffsetAndLength& InOffLen,
			const TArray<FIoStoreTocCompressedBlockEntry>& InBlocks,
			uint32 InBlockSize,
			TArray<uint8>& OutData) -> bool
		{
			const int64 VirtualOffset = (int64)InOffLen.GetOffset();
			const int64 VirtualLength = (int64)InOffLen.GetLength();
			if (VirtualLength == 0) { OutData.Reset(); return true; }

			const int32 FirstBlock = (int32)(VirtualOffset / InBlockSize);
			const int32 LastBlock  = (int32)((Align(VirtualOffset + VirtualLength, (int64)InBlockSize) - 1) / InBlockSize);
			if (FirstBlock >= InBlocks.Num() || LastBlock >= InBlocks.Num()) return false;

			const int64 PhysStart = (int64)InBlocks[FirstBlock].GetOffset();
			const int64 PhysEnd   = (int64)InBlocks[LastBlock].GetOffset() +
				Align((int64)InBlocks[LastBlock].GetCompressedSize(), (int64)FAES::AESBlockSize);
			const int64 PhysSize = PhysEnd - PhysStart;
			if (PhysStart + PhysSize > OldUcasFileSizeForReader) return false;

			OldUcasReader->Seek(PhysStart);
			OutData.SetNumUninitialized(PhysSize);
			OldUcasReader->Serialize(OutData.GetData(), PhysSize);
			return !OldUcasReader->IsError();
		};

		// Helper: 通过 CompressionBlocks 物理寻址读取 chunk 数据（原始字节，不做预处理）
		auto ReadChunkPhysical = [](const FString& InUcasFile,
			const FIoOffsetAndLength& InOffLen,
			const TArray<FIoStoreTocCompressedBlockEntry>& InBlocks,
			uint32 InBlockSize,
			TArray<uint8>& OutData) -> bool
		{
			const int64 VirtualOffset = (int64)InOffLen.GetOffset();
			const int64 VirtualLength = (int64)InOffLen.GetLength();
			if (VirtualLength == 0) { OutData.Reset(); return true; }

			const int32 FirstBlock = (int32)(VirtualOffset / InBlockSize);
			const int32 LastBlock  = (int32)((Align(VirtualOffset + VirtualLength, (int64)InBlockSize) - 1) / InBlockSize);
			if (FirstBlock >= InBlocks.Num() || LastBlock >= InBlocks.Num()) return false;

			const int64 PhysStart = (int64)InBlocks[FirstBlock].GetOffset();
			const int64 PhysEnd   = (int64)InBlocks[LastBlock].GetOffset() +
				Align((int64)InBlocks[LastBlock].GetCompressedSize(), (int64)FAES::AESBlockSize);
			return ReadUcasRange(InUcasFile, PhysStart, PhysEnd - PhysStart, OutData);
		};

		// Helper: 解密 chunk 内所有 blocks（就地）
		auto DecryptChunkBlocks = [&EncKey](TArray<uint8>& InOutData,
			const FIoOffsetAndLength& InOffLen,
			const TArray<FIoStoreTocCompressedBlockEntry>& InBlocks,
			uint32 InBlockSize) -> void
		{
			if (!EncKey) return;
			const int64 VirtualOffset = (int64)InOffLen.GetOffset();
			const int64 VirtualLength = (int64)InOffLen.GetLength();
			const int32 FirstBlock = (int32)(VirtualOffset / InBlockSize);
			const int32 LastBlock  = (int32)((Align(VirtualOffset + VirtualLength, (int64)InBlockSize) - 1) / InBlockSize);
			const int64 PhysStart = (int64)InBlocks[FirstBlock].GetOffset();
			for (int32 Bi = FirstBlock; Bi <= LastBlock; ++Bi)
			{
				const int64 BlockOff = (int64)InBlocks[Bi].GetOffset() - PhysStart;
				const uint32 RawSize = Align(InBlocks[Bi].GetCompressedSize(), (uint32)FAES::AESBlockSize);
				FAES::DecryptData(InOutData.GetData() + BlockOff, RawSize, *EncKey);
			}
		};

		// Helper: 解压 chunk 内所有 blocks → 拼出 uncompressed 数据
		auto DecompressChunkBlocks = [](const TArray<uint8>& InCompData,
			const FIoOffsetAndLength& InOffLen,
			const TArray<FIoStoreTocCompressedBlockEntry>& InBlocks,
			const TArray<FName>& InCompressionMethods,
			uint32 InBlockSize,
			TArray<uint8>& OutUncomp) -> bool
		{
			const int64 VirtualOffset = (int64)InOffLen.GetOffset();
			const int64 VirtualLength = (int64)InOffLen.GetLength();
			const int32 FirstBlock = (int32)(VirtualOffset / InBlockSize);
			const int32 LastBlock  = (int32)((Align(VirtualOffset + VirtualLength, (int64)InBlockSize) - 1) / InBlockSize);
			const int64 PhysStart = (int64)InBlocks[FirstBlock].GetOffset();

			OutUncomp.SetNumUninitialized(VirtualLength);
			int64 DstOffset = 0;
			for (int32 Bi = FirstBlock; Bi <= LastBlock; ++Bi)
			{
				const int64 BlockOff = (int64)InBlocks[Bi].GetOffset() - PhysStart;
				const uint32 CompSize = InBlocks[Bi].GetCompressedSize();
				const uint32 UncompSize = InBlocks[Bi].GetUncompressedSize();
				const uint8 MethodIdx = InBlocks[Bi].GetCompressionMethodIndex();
				const int64 WriteSize = FMath::Min<int64>((int64)UncompSize, VirtualLength - DstOffset);
				if (WriteSize <= 0) break;

				if (MethodIdx == 0 || InCompressionMethods.Num() == 0)
				{
					FMemory::Memcpy(OutUncomp.GetData() + DstOffset, InCompData.GetData() + BlockOff, WriteSize);
				}
				else
				{
					const FName MethodName = (MethodIdx < (uint8)InCompressionMethods.Num()) ?
						InCompressionMethods[MethodIdx] : NAME_None;
					if (!FCompression::UncompressMemory(MethodName,
						OutUncomp.GetData() + DstOffset, (int32)WriteSize,
						InCompData.GetData() + BlockOff, (int32)CompSize))
					{
						return false;
					}
				}
				DstOffset += WriteSize;
			}
			return true;
		};

		// Helper: 对 chunk 覆盖的 blocks 重新压缩（从 uncompressed → compressed，按 NewToc block 布局）
		auto RecompressChunkBlocks = [](const TArray<uint8>& InUncomp,
			const FIoOffsetAndLength& InOffLen,
			const TArray<FIoStoreTocCompressedBlockEntry>& InBlocks,
			const TArray<FName>& InCompressionMethods,
			uint32 InBlockSize,
			TArray<uint8>& OutComp) -> bool
		{
			const int64 VirtualOffset = (int64)InOffLen.GetOffset();
			const int64 VirtualLength = (int64)InOffLen.GetLength();
			const int32 FirstBlock = (int32)(VirtualOffset / InBlockSize);
			const int32 LastBlock  = (int32)((Align(VirtualOffset + VirtualLength, (int64)InBlockSize) - 1) / InBlockSize);
			const int64 PhysStart = (int64)InBlocks[FirstBlock].GetOffset();
			const int64 PhysEnd   = (int64)InBlocks[LastBlock].GetOffset() +
				Align((int64)InBlocks[LastBlock].GetCompressedSize(), (int64)FAES::AESBlockSize);
			OutComp.SetNumZeroed(PhysEnd - PhysStart);

			int64 SrcOffset = 0;
			for (int32 Bi = FirstBlock; Bi <= LastBlock; ++Bi)
			{
				const int64 BlockOff = (int64)InBlocks[Bi].GetOffset() - PhysStart;
				const uint32 CompSize = InBlocks[Bi].GetCompressedSize();
				const uint32 UncompSize = InBlocks[Bi].GetUncompressedSize();
				const uint8 MethodIdx = InBlocks[Bi].GetCompressionMethodIndex();
				const int64 SrcSize = FMath::Min<int64>((int64)UncompSize, VirtualLength - SrcOffset);
				if (SrcSize <= 0) break;

				if (MethodIdx == 0 || InCompressionMethods.Num() == 0)
				{
					FMemory::Memcpy(OutComp.GetData() + BlockOff, InUncomp.GetData() + SrcOffset, SrcSize);
				}
				else
				{
					const FName MethodName = (MethodIdx < (uint8)InCompressionMethods.Num()) ?
						InCompressionMethods[MethodIdx] : NAME_None;

					// Oodle 等压缩器需要 worst-case 大小的输出 buffer
					int32 BoundSize = FCompression::CompressMemoryBound(MethodName, (int32)SrcSize);
					TArray<uint8> TempCompBuf;
					TempCompBuf.SetNumUninitialized(BoundSize);
					int32 ActualCompSize = BoundSize;

					if (FCompression::CompressMemory(MethodName,
						TempCompBuf.GetData(), ActualCompSize,
						InUncomp.GetData() + SrcOffset, (int32)SrcSize) &&
						ActualCompSize <= (int32)CompSize)
					{
						// 压缩成功且能塞进原 block 槽位
						FMemory::Memcpy(OutComp.GetData() + BlockOff, TempCompBuf.GetData(), ActualCompSize);
					}
					else
					{
						// 重压缩失败 / 结果超过原槽位：utoc 仍标记该 block 为 Oodle 压缩，运行时解压会崩。
						// 不能把未压缩字节塞回去，整 chunk 失败让上层重试。
						UE_LOG(LogPPakPacher, Error,
							TEXT("PatchIoStore::RecompressChunkBlocks - Recompress block %d failed or oversized "
								 "(SrcSize=%lld OrigCompSize=%u ActualCompSize=%d Method=%s). Aborting chunk."),
							Bi, SrcSize, CompSize, ActualCompSize, *MethodName.ToString());
						return false;
					}
				}
				SrcOffset += SrcSize;
			}
			return true;
		};

		// Helper: 对 chunk 覆盖的 blocks 重新加密（就地）
		auto EncryptChunkBlocks = [&EncKey](TArray<uint8>& InOutData,
			const FIoOffsetAndLength& InOffLen,
			const TArray<FIoStoreTocCompressedBlockEntry>& InBlocks,
			uint32 InBlockSize) -> void
		{
			if (!EncKey) return;
			const int64 VirtualOffset = (int64)InOffLen.GetOffset();
			const int64 VirtualLength = (int64)InOffLen.GetLength();
			const int32 FirstBlock = (int32)(VirtualOffset / InBlockSize);
			const int32 LastBlock  = (int32)((Align(VirtualOffset + VirtualLength, (int64)InBlockSize) - 1) / InBlockSize);
			const int64 PhysStart = (int64)InBlocks[FirstBlock].GetOffset();
			for (int32 Bi = FirstBlock; Bi <= LastBlock; ++Bi)
			{
				const int64 BlockOff = (int64)InBlocks[Bi].GetOffset() - PhysStart;
				const uint32 RawSize = Align(InBlocks[Bi].GetCompressedSize(), (uint32)FAES::AESBlockSize);
				FAES::EncryptData(InOutData.GetData() + BlockOff, RawSize, *EncKey);
			}
		};

		// Helper: 获取 chunk 在 ucas 中的物理起始位置（用于写入目标位置）
		auto GetChunkPhysicalOffset = [](const FIoOffsetAndLength& InOffLen,
			const TArray<FIoStoreTocCompressedBlockEntry>& InBlocks,
			uint32 InBlockSize) -> int64
		{
			const int64 VirtualOffset = (int64)InOffLen.GetOffset();
			const int32 FirstBlock = (int32)(VirtualOffset / InBlockSize);
			if (FirstBlock >= InBlocks.Num()) return -1;
			return (int64)InBlocks[FirstBlock].GetOffset();
		};

		// 构建 NewChunkId → NewToc Index 映射
		TMap<FIoChunkId, int32> NewChunkIdxMap;
		for (int32 i = 0; i < NewToc.ChunkIds.Num(); ++i)
		{
			NewChunkIdxMap.Add(NewToc.ChunkIds[i], i);
		}

		// 构建 OldChunkId → OldToc Index 映射
		TMap<FIoChunkId, int32> OldChunkIdxMap;
		for (int32 i = 0; i < OldToc.ChunkIds.Num(); ++i)
		{
			OldChunkIdxMap.Add(OldToc.ChunkIds[i], i);
		}

		// 预计算每个 chunk 的 WriteOffset，并按 WriteOffset 升序排序，
		// 使流式写入可行（避免全量 buffer）。
		struct FChunkWriteTask
		{
			const FPIoStoreChunkPatchInfo* Info;
			int64 WriteOffset;
		};
		TArray<FChunkWriteTask> SortedTasks;
		SortedTasks.Reserve(Body->ChunkPatchInfos.Num());
		for (const FPIoStoreChunkPatchInfo& ChunkInfo : Body->ChunkPatchInfos)
		{
			if (ChunkInfo.PatchType == EIoStoreChunkPatchType::Delete)
				continue;

			int64 WO = -1;
			const int32* NewIdxPtr = NewChunkIdxMap.Find(ChunkInfo.ChunkId);
			if (NewIdxPtr)
			{
				WO = GetChunkPhysicalOffset(NewToc.ChunkOffsetLengths[*NewIdxPtr], NewToc.CompressionBlocks, NewBlockSize);
			}
			else
			{
				WO = ChunkInfo.NewOffset;
			}
			if (WO < 0)
			{
				UE_LOG(LogPPakPacher, Error, TEXT("PatchIoStore - Failed to determine write offset for chunk (sort phase)"));
				return false;
			}
			SortedTasks.Add({ &ChunkInfo, WO });
		}
		SortedTasks.Sort([](const FChunkWriteTask& A, const FChunkWriteTask& B) { return A.WriteOffset < B.WriteOffset; });

		for (const FChunkWriteTask& Task : SortedTasks)
		{
			const FPIoStoreChunkPatchInfo& ChunkInfo = *Task.Info;
			const int64 WriteOffset = Task.WriteOffset;
			const int32* NewIdxPtr = NewChunkIdxMap.Find(ChunkInfo.ChunkId);

			if (ChunkInfo.PatchType == EIoStoreChunkPatchType::Keep)
			{
				// Keep：从 old ucas 物理位置读取并直接写入 new 位置（保持加密态）
				const int32* OldIdxPtr = OldChunkIdxMap.Find(ChunkInfo.ChunkId);
				if (!OldIdxPtr) { return false; }
				TArray<uint8> OldData;
				PPATCHER_PERF_BEGIN(InOutPerfReport, TimeRead);
				const bool bReadOk = ReadChunkPhysicalReused(OldToc.ChunkOffsetLengths[*OldIdxPtr], OldToc.CompressionBlocks, OldBlockSize, OldData);
				PPATCHER_PERF_END(InOutPerfReport, TimeRead);
				if (!bReadOk)
				{
					UE_LOG(LogPPakPacher, Error, TEXT("PatchIoStore - Read old Keep chunk failed"));
					return false;
				}
				if (WriteOffset + OldData.Num() > NewUcasPhysicalSize)
				{
					UE_LOG(LogPPakPacher, Error, TEXT("PatchIoStore - Keep chunk exceeds new ucas size"));
					return false;
				}
				// 断言写入偏移单调递增
				if (WriteOffset < LastWriteEnd)
				{
					UE_LOG(LogPPakPacher, Error, TEXT("PatchIoStore - WriteOffset regression! WriteOffset=%lld LastWriteEnd=%lld"), WriteOffset, LastWriteEnd);
					return false;
				}
				// 填充 gap（chunk 间 alignment padding） — 修复 #12 用复用的 WriteGap
				if (WriteOffset > LastWriteEnd)
				{
					WriteGap(LastWriteEnd, WriteOffset - LastWriteEnd);
				}
				UcasWriter->Seek(WriteOffset);
				UcasWriter->Serialize(OldData.GetData(), OldData.Num());
				LastWriteEnd = WriteOffset + OldData.Num();
			}
			else if (ChunkInfo.PatchType == EIoStoreChunkPatchType::Modify)
			{
				const int32* OldIdxPtr = OldChunkIdxMap.Find(ChunkInfo.ChunkId);
				if (!OldIdxPtr) { return false; }
				TArray<uint8> OldData;
				{
					PPATCHER_PERF_BEGIN(InOutPerfReport, TimeRead);
					const bool bReadOk = ReadChunkPhysicalReused(OldToc.ChunkOffsetLengths[*OldIdxPtr], OldToc.CompressionBlocks, OldBlockSize, OldData);
					PPATCHER_PERF_END(InOutPerfReport, TimeRead);
					if (!bReadOk)
					{
						UE_LOG(LogPPakPacher, Error, TEXT("PatchIoStore - Read old Modify chunk failed"));
						return false;
					}
				}

				// Patch 端预处理：解密 old 数据（与 Create 侧对齐）
				if (bDecrypt && bOldEncrypted)
				{
					PPATCHER_PERF_BEGIN(InOutPerfReport, TimeDecrypt);
					DecryptChunkBlocks(OldData, OldToc.ChunkOffsetLengths[*OldIdxPtr], OldToc.CompressionBlocks, OldBlockSize);
					PPATCHER_PERF_END(InOutPerfReport, TimeDecrypt);
				}

				// DecryptAndDecompress：进一步解压
				TArray<uint8> OldPatchInput;
				if (bDecompress)
				{
					PPATCHER_PERF_BEGIN(InOutPerfReport, TimeDecompress);
					const bool bDecOk = DecompressChunkBlocks(OldData, OldToc.ChunkOffsetLengths[*OldIdxPtr],
						OldToc.CompressionBlocks, OldToc.CompressionMethods, OldBlockSize, OldPatchInput);
					PPATCHER_PERF_END(InOutPerfReport, TimeDecompress);
					if (!bDecOk)
					{
						UE_LOG(LogPPakPacher, Error, TEXT("PatchIoStore - Decompress old Modify chunk failed"));
						return false;
					}
				}
				else
				{
					OldPatchInput = MoveTemp(OldData);
				}

				TArray<uint8> DiffData;
				{
					PPATCHER_PERF_BEGIN(InOutPerfReport, TimeRead);
					const bool bGetOk = const_cast<FPResPatchData&>(InPatch).GetFilePatchData(
						const_cast<FPPakPatchDataInfo&>(ChunkInfo.DataInfo), DiffData);
					PPATCHER_PERF_END(InOutPerfReport, TimeRead);
					if (!bGetOk)
					{
						UE_LOG(LogPPakPacher, Error, TEXT("PatchIoStore - Read chunk diff data failed"));
						return false;
					}
				}

				// 应用 HDiff patch → 得到 new 预处理态数据
				const int64 NewPatchedSize = ChunkInfo.DataInfo.NewSize;
				TArray<uint8> ChunkNewData;
				ChunkNewData.SetNumUninitialized(NewPatchedSize);
				{
					PPATCHER_PERF_BEGIN(InOutPerfReport, TimePatch);
					const bool bPatchOk = BinPatcher->Patch(ChunkNewData.GetData(), (uint64)ChunkNewData.Num(),
						OldPatchInput.GetData(), (uint64)OldPatchInput.Num(),
						DiffData.GetData(), (uint64)DiffData.Num());
					PPATCHER_PERF_END(InOutPerfReport, TimePatch);
					if (!bPatchOk)
					{
						UE_LOG(LogPPakPacher, Error, TEXT("PatchIoStore - Chunk Patch failed"));
						return false;
					}
				}

				// 逆处理：重新压缩（DecryptAndDecompress 时）
				if (bDecompress && NewIdxPtr)
				{
					TArray<uint8> RecompData;
					PPATCHER_PERF_BEGIN(InOutPerfReport, TimeCompress);
					const bool bRecOk = RecompressChunkBlocks(ChunkNewData, NewToc.ChunkOffsetLengths[*NewIdxPtr],
						NewToc.CompressionBlocks, NewToc.CompressionMethods, NewBlockSize, RecompData);
					PPATCHER_PERF_END(InOutPerfReport, TimeCompress);
					if (!bRecOk)
					{
						UE_LOG(LogPPakPacher, Warning, TEXT("PatchIoStore - Recompress Modify chunk failed, using raw"));
					}
					else
					{
						ChunkNewData = MoveTemp(RecompData);
					}
				}

				// 逆处理：重新加密（DecryptAndCompress / DecryptAndDecompress 时）
				if (bDecrypt && bNewEncrypted && NewIdxPtr)
				{
					PPATCHER_PERF_BEGIN(InOutPerfReport, TimeEncrypt);
					EncryptChunkBlocks(ChunkNewData, NewToc.ChunkOffsetLengths[*NewIdxPtr], NewToc.CompressionBlocks, NewBlockSize);
					PPATCHER_PERF_END(InOutPerfReport, TimeEncrypt);
				}

				if (WriteOffset + ChunkNewData.Num() > NewUcasPhysicalSize)
				{
					UE_LOG(LogPPakPacher, Error, TEXT("PatchIoStore - Modify chunk exceeds new ucas size"));
					return false;
				}
				// 断言写入偏移单调递增
				if (WriteOffset < LastWriteEnd)
				{
					UE_LOG(LogPPakPacher, Error, TEXT("PatchIoStore - WriteOffset regression (Modify)! WriteOffset=%lld LastWriteEnd=%lld"), WriteOffset, LastWriteEnd);
					return false;
				}
				if (WriteOffset > LastWriteEnd)
				{
					WriteGap(LastWriteEnd, WriteOffset - LastWriteEnd);
				}
				UcasWriter->Seek(WriteOffset);
				UcasWriter->Serialize(ChunkNewData.GetData(), ChunkNewData.Num());
				LastWriteEnd = WriteOffset + ChunkNewData.Num();
			}
			else if (ChunkInfo.PatchType == EIoStoreChunkPatchType::New)
			{
				TArray<uint8> FullData;
				{
					PPATCHER_PERF_BEGIN(InOutPerfReport, TimeRead);
					const bool bGetOk = const_cast<FPResPatchData&>(InPatch).GetFilePatchData(
						const_cast<FPPakPatchDataInfo&>(ChunkInfo.DataInfo), FullData);
					PPATCHER_PERF_END(InOutPerfReport, TimeRead);
					if (!bGetOk)
					{
						UE_LOG(LogPPakPacher, Error, TEXT("PatchIoStore - Read new chunk full data failed"));
						return false;
					}
				}

				// New chunk 的逆处理：重新压缩 + 重新加密
				if (bDecompress && NewIdxPtr)
				{
					TArray<uint8> RecompData;
					PPATCHER_PERF_BEGIN(InOutPerfReport, TimeCompress);
					const bool bRecOk = RecompressChunkBlocks(FullData, NewToc.ChunkOffsetLengths[*NewIdxPtr],
						NewToc.CompressionBlocks, NewToc.CompressionMethods, NewBlockSize, RecompData);
					PPATCHER_PERF_END(InOutPerfReport, TimeCompress);
					if (!bRecOk)
					{
						UE_LOG(LogPPakPacher, Warning, TEXT("PatchIoStore - Recompress New chunk failed, using raw"));
					}
					else
					{
						FullData = MoveTemp(RecompData);
					}
				}
				if (bDecrypt && bNewEncrypted && NewIdxPtr)
				{
					PPATCHER_PERF_BEGIN(InOutPerfReport, TimeEncrypt);
					EncryptChunkBlocks(FullData, NewToc.ChunkOffsetLengths[*NewIdxPtr], NewToc.CompressionBlocks, NewBlockSize);
					PPATCHER_PERF_END(InOutPerfReport, TimeEncrypt);
				}

				if (WriteOffset + FullData.Num() > NewUcasPhysicalSize)
				{
					UE_LOG(LogPPakPacher, Error, TEXT("PatchIoStore - New chunk exceeds new ucas size"));
					return false;
				}
				// 断言写入偏移单调递增
				if (WriteOffset < LastWriteEnd)
				{
					UE_LOG(LogPPakPacher, Error, TEXT("PatchIoStore - WriteOffset regression (New)! WriteOffset=%lld LastWriteEnd=%lld"), WriteOffset, LastWriteEnd);
					return false;
				}
				if (WriteOffset > LastWriteEnd)
				{
					WriteGap(LastWriteEnd, WriteOffset - LastWriteEnd);
				}
				UcasWriter->Seek(WriteOffset);
				UcasWriter->Serialize(FullData.GetData(), FullData.Num());
				LastWriteEnd = WriteOffset + FullData.Num();
			}
		}

		// 补尾部 padding（如果 LastWriteEnd < NewUcasPhysicalSize） — 复用 WriteGap
		if (LastWriteEnd < NewUcasPhysicalSize)
		{
			WriteGap(LastWriteEnd, NewUcasPhysicalSize - LastWriteEnd);
		}

		{
			PPATCHER_PERF_BEGIN(InOutPerfReport, TimeWrite);
			UcasWriter->Close();
			UcasWriter.Reset();
			// 关闭复用的 OldUcasReader，避免后续 verify 阶段文件被占用
			OldUcasReader->Close();
			OldUcasReader.Reset();
			PPATCHER_PERF_END(InOutPerfReport, TimeWrite);

			// 验证最终文件大小
			const int64 ActualSize = IFileManager::Get().FileSize(*NewUcas);
			if (ActualSize != NewUcasPhysicalSize)
			{
				UE_LOG(LogPPakPacher, Error, TEXT("PatchIoStore - New ucas size mismatch. Expected=%lld Actual=%lld File=%s"),
					NewUcasPhysicalSize, ActualSize, *NewUcas);
				return false;
			}
		}
	}
	else
	{
		// FileBinary：.ucas 整体 diff 还原
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
			PPATCHER_PERF_BEGIN(InOutPerfReport, TimePatch);
			const bool bApplyOk = ApplyFileDiff(BinPatcher, InPatch, const_cast<FPPakPatchDataInfo&>(Body->UcasDiffInfo), NewUcas, OldUcas);
			PPATCHER_PERF_END(InOutPerfReport, TimePatch); // ucas FileBinary Patch 阶段（含读、Patch、写）整体记到 TimePatch
			if (!bApplyOk)
			{
				return false;
			}
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// CheckIoStoreDiff
// ---------------------------------------------------------------------------

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

	if (Body->Strategy == EIoStoreDiffStrategy::FileBinary)
	{
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
	}
	else
	{
		// ChunkAware：校验 new ucas 整体 hash
		if (!FPPakPatcherUtils::VerifyFileHashByCheckType(NewUcas,
			Body->NewUcasMD5, Body->NewUcasCrc32,
			TEXT("FPIoStorePatcher::CheckIoStoreDiff/NewUcas")))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPIoStorePatcher::CheckIoStoreDiff - .ucas hash mismatch (ChunkAware)."));
			return false;
		}
	}

	return true;
}
