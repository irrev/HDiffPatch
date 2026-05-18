#include "Patcher/PPakPatcher.h"
#include "PPakPatcherModule.h"
#include "Patcher/PIoStorePatcher.h"
#include "Data/PPakPatcherDataType.h"
#include "Data/PPakPatcherKeyChainHelper.h"
#include "Archive/PPakMemoryArchive.h"
#include "Archive/PSignedPakPatchWriter.h"
#include "Utils/PPakPatcherUtils.h"
#include "Utils/PDebugMemoryBank.h"
#include "PPakPatcherSettings.h"

#if WITH_EDITOR
#include "PakFileUtilities.h"
#endif
//begin engine heads
#include "Misc/AES.h"
#include "Misc/ICompressionFormat.h"
#include "Misc/KeyChainUtilities.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Serialization/MemoryReader.h"

int64 CalcEntryRealSize(const FPakEntry& Entry, const FPakFile& PakFile)
{
	int64 RealSize = 0;
	bool bIsCompressed = Entry.CompressionMethodIndex != 0;

	if (bIsCompressed)
	{
		check(Entry.CompressionBlocks.Num() > 0);
		RealSize = Entry.GetSerializedSize(PakFile.GetInfo().Version);
		for (int32 i=0; i< Entry.CompressionBlocks.Num(); ++i)
		{
			
			int32 BlockSize = Entry.CompressionBlocks[i].CompressedEnd - Entry.CompressionBlocks[i].CompressedStart;
			RealSize += Entry.IsEncrypted() ? Align(BlockSize, FAES::AESBlockSize) : BlockSize;
		}
	}
	else
	{
		RealSize = Entry.GetSerializedSize(PakFile.GetInfo().Version);
		RealSize += Entry.IsEncrypted() ? Align(Entry.Size, FAES::AESBlockSize) : Entry.Size;
	}
	return RealSize;
}

/**
 * 就地解密/加密 entry 的 Data 部分（跳过 EntryHeader）。
 * 核心实现：通过 EntryHeaderSize 精确定位 Data 起始位置。
 */
static bool CryptEntryDataImpl(uint8* InOutBuffer, int64 InBufferSize,
	int64 InEntryHeaderSize, bool bEncrypt, const FAES::FAESKey& InKey)
{
	uint8* DataStart = InOutBuffer + InEntryHeaderSize;
	const int64 DataSize = InBufferSize - InEntryHeaderSize;

	if (DataSize <= 0)
	{
		return false;
	}

	const int64 AlignedSize = Align(DataSize, FAES::AESBlockSize);
	if (bEncrypt)
	{
		FAES::EncryptData(DataStart, AlignedSize, InKey);
	}
	else
	{
		FAES::DecryptData(DataStart, AlignedSize, InKey);
	}
	return true;
}

/** 就地解密 entry 的 Data 部分。Version 从 FPakFile 获取。 */
static bool DecryptEntryData(uint8* InOutBuffer, int64 InBufferSize,
	const FPakEntry& InEntry, const FPakFile& InPakFile, const FAES::FAESKey& InKey)
{
	if (!InEntry.IsEncrypted()) return true;
	const int64 HeaderSize = InEntry.GetSerializedSize(InPakFile.GetInfo().Version);
	return CryptEntryDataImpl(InOutBuffer, InBufferSize, HeaderSize, /*bEncrypt=*/false, InKey);
}

/** 就地解密 entry 的 Data 部分。Version 显式传入（无需 FPakFile 对象）。 */
static bool DecryptEntryData(uint8* InOutBuffer, int64 InBufferSize,
	const FPakEntry& InEntry, int32 InPakVersion, const FAES::FAESKey& InKey)
{
	if (!InEntry.IsEncrypted()) return true;
	const int64 HeaderSize = InEntry.GetSerializedSize(InPakVersion);
	return CryptEntryDataImpl(InOutBuffer, InBufferSize, HeaderSize, /*bEncrypt=*/false, InKey);
}

/** 就地加密 entry 的 Data 部分。Version 从 FPakFile 获取。 */
static bool EncryptEntryData(uint8* InOutBuffer, int64 InBufferSize,
	const FPakEntry& InEntry, const FPakFile& InPakFile, const FAES::FAESKey& InKey)
{
	if (!InEntry.IsEncrypted()) return true;
	const int64 HeaderSize = InEntry.GetSerializedSize(InPakFile.GetInfo().Version);
	return CryptEntryDataImpl(InOutBuffer, InBufferSize, HeaderSize, /*bEncrypt=*/true, InKey);
}

/** 就地加密 entry 的 Data 部分。Version 显式传入（无需 FPakFile 对象）。 */
static bool EncryptEntryData(uint8* InOutBuffer, int64 InBufferSize,
	const FPakEntry& InEntry, int32 InPakVersion, const FAES::FAESKey& InKey)
{
	if (!InEntry.IsEncrypted()) return true;
	const int64 HeaderSize = InEntry.GetSerializedSize(InPakVersion);
	return CryptEntryDataImpl(InOutBuffer, InBufferSize, HeaderSize, /*bEncrypt=*/true, InKey);
}

/**
 * 从 KeyChain 中查找匹配 EncryptionKeyGuid 的 key。
 * 如果 Guid 为空，返回 PrincipalEncryptionKey 或第一个可用 key。
 */
static const FAES::FAESKey* FindEncryptionKey(const FKeyChain& InKeyChain, const FGuid& InGuid)
{
	if (InGuid.IsValid())
	{
		const FNamedAESKey* Found = InKeyChain.GetEncryptionKeys().Find(InGuid);
		return Found ? &Found->Key : nullptr;
	}
	// Guid 为空：取 Principal 或第一个
	if (const FNamedAESKey* Principal = InKeyChain.GetPrincipalEncryptionKey())
	{
		return &Principal->Key;
	}
	for (auto It = InKeyChain.GetEncryptionKeys().CreateConstIterator(); It; ++It)
	{
		return &It.Value().Key;
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Secondary Index meta 解析（v10+）
// ---------------------------------------------------------------------------

namespace PPakPatcherPrivate
{
	/**
	 * 从 pak 读取 PrimaryIndex 原始字节（可能加密）。
	 */
	static bool ReadPrimaryIndexRaw(FArchive& PakArchive, const FPakInfo& Info, TArray<uint8>& OutRaw)
	{
		if (Info.IndexOffset < 0 || Info.IndexSize <= 0 || Info.IndexOffset + Info.IndexSize > PakArchive.TotalSize())
		{
			return false;
		}
		OutRaw.SetNumUninitialized((int32)Info.IndexSize);
		PakArchive.Seek(Info.IndexOffset);
		PakArchive.Serialize(OutRaw.GetData(), Info.IndexSize);
		return true;
	}

	/**
	 * 通用索引字节解密 + SHA1 校验。
	 *   - 若 Info.bEncryptedIndex=true，用 KeyChain 逐 key 试错就地解密 InOutIndexData；解密后 SHA1 与 InExpectedHash 对比。
	 *   - 若不加密，直接对 InOutIndexData 做 SHA1，与 InExpectedHash 对比。
	 *
	 * 用于 Primary / PathHash / FullDirectory 三段独立 Hash 校验。
	 */
	static bool DecryptAndValidateIndexBytes(const FPakInfo& Info, TArray<uint8>& InOutIndexData, const FSHAHash& InExpectedHash)
	{
		if (!Info.bEncryptedIndex)
		{
			FSHAHash Actual;
			FSHA1::HashBuffer(InOutIndexData.GetData(), InOutIndexData.Num(), Actual.Hash);
			return Actual == InExpectedHash;
		}

		FKeyChain& KeyChain = FPPakPatcherKeyChainHelper::Get().GetKeyChain();

		// 保存一份密文副本，key 试错时用
		TArray<uint8> CipherCopy = InOutIndexData;

		auto TryOneKey = [&](const FNamedAESKey& NamedKey) -> bool
		{
			InOutIndexData = CipherCopy;
			FAES::DecryptData(InOutIndexData.GetData(), InOutIndexData.Num(), NamedKey.Key);
			FSHAHash Actual;
			FSHA1::HashBuffer(InOutIndexData.GetData(), InOutIndexData.Num(), Actual.Hash);
			return Actual == InExpectedHash;
		};

		if (KeyChain.GetPrincipalEncryptionKey() && TryOneKey(*KeyChain.GetPrincipalEncryptionKey()))
		{
			return true;
		}
		for (auto It = KeyChain.GetEncryptionKeys().CreateConstIterator(); It; ++It)
		{
			if (TryOneKey(It.Value()))
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * 用 KeyChain 尝试解密 PrimaryIndex；不加密时直接返回 true。
	 * 校验 SHA1 与 Info.IndexHash 一致。
	 */
	static bool DecryptAndValidatePrimaryIndex(const FPakInfo& Info, TArray<uint8>& InOutIndexData)
	{
		return DecryptAndValidateIndexBytes(Info, InOutIndexData, Info.IndexHash);
	}

	/**
	 * 从已解密的 PrimaryIndex 字节流中解析出两个 Secondary Index 的 Offset/Size/Hash。
	 */
	struct FSecondaryIndexMeta
	{
		bool    bHasPathHashIndex      = false;
		int64   PathHashIndexOffset    = INDEX_NONE;
		int64   PathHashIndexSize      = 0;
		FSHAHash PathHashIndexHash;

		bool    bHasFullDirectoryIndex = false;
		int64   FullDirectoryIndexOffset = INDEX_NONE;
		int64   FullDirectoryIndexSize   = 0;
		FSHAHash FullDirectoryIndexHash;
	};

	/**
	 * 解析 PrimaryIndex 得到 Secondary Index meta。
	 * 参考 Engine/Source/Runtime/PakFile/Private/PakFile.cpp::LoadIndexInternal 的序列化顺序。
	 */
	static bool ParseSecondaryIndexMeta(const TArray<uint8>& DecryptedPrimary, FSecondaryIndexMeta& OutMeta)
	{
		FMemoryReader Reader(DecryptedPrimary);

		FString MountPoint;
		Reader << MountPoint;
		if (MountPoint.Len() > 65535)
		{
			return false;
		}

		int32 NumEntries = 0;
		Reader << NumEntries;
		if (NumEntries < 0)
		{
			return false;
		}

		uint64 PathHashSeed = 0;
		Reader << PathHashSeed;

		Reader << OutMeta.bHasPathHashIndex;
		if (OutMeta.bHasPathHashIndex)
		{
			Reader << OutMeta.PathHashIndexOffset;
			Reader << OutMeta.PathHashIndexSize;
			Reader << OutMeta.PathHashIndexHash;
			if (OutMeta.PathHashIndexOffset == INDEX_NONE)
			{
				OutMeta.bHasPathHashIndex = false;
			}
		}

		Reader << OutMeta.bHasFullDirectoryIndex;
		if (OutMeta.bHasFullDirectoryIndex)
		{
			Reader << OutMeta.FullDirectoryIndexOffset;
			Reader << OutMeta.FullDirectoryIndexSize;
			Reader << OutMeta.FullDirectoryIndexHash;
			if (OutMeta.FullDirectoryIndexOffset == INDEX_NONE)
			{
				OutMeta.bHasFullDirectoryIndex = false;
			}
		}

		return !Reader.IsError();
	}

	/**
	 * 对给定 pak 的 FPakInfo，读取 PrimaryIndex 并解析 Secondary meta。
	 * 需要 Version >= PakFile_Version_PathHashIndex，否则返回 false（项目目标只支持 v10+）。
	 */
	static bool LoadSecondaryIndexMeta(FArchive& PakArchive, const FPakInfo& Info, FSecondaryIndexMeta& OutMeta)
	{
		if (Info.Version < FPakInfo::PakFile_Version_PathHashIndex)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("LoadSecondaryIndexMeta - Pak version %d < PathHashIndex(10); legacy layout unsupported."), Info.Version);
			return false;
		}

		TArray<uint8> PrimaryIndexData;
		if (!ReadPrimaryIndexRaw(PakArchive, Info, PrimaryIndexData))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("LoadSecondaryIndexMeta - Read PrimaryIndex raw failed."));
			return false;
		}
		if (!DecryptAndValidatePrimaryIndex(Info, PrimaryIndexData))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("LoadSecondaryIndexMeta - Decrypt/Validate PrimaryIndex failed."));
			return false;
		}
		if (!ParseSecondaryIndexMeta(PrimaryIndexData, OutMeta))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("LoadSecondaryIndexMeta - Parse SecondaryIndex meta failed."));
			return false;
		}
		return true;
	}
}

// ---------------------------------------------------------------------------
// FPPakPatcher : 文件路径接口（委托到 FPPakFileDataPtr 接口）
// ---------------------------------------------------------------------------

FPPakFileDataPtr FPPakPatcher::LoadOrGetPakData(const FString& InPakFilename)
{
	if (InPakFilename.IsEmpty())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::LoadOrGetPakData - Empty pak filename."));
		return nullptr;
	}

	FString Standardized = InPakFilename;
	FPaths::MakeStandardFilename(Standardized);

	if (FPPakFileDataPtr* Found = CachedPakData.Find(Standardized))
	{
		return *Found;
	}

	FPPakFileDataPtr NewData = MakeShared<FPPakFileData, ESPMode::ThreadSafe>();
	if (!NewData->LoadFromFile(Standardized))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::LoadOrGetPakData - Failed to load pak: %s"), *Standardized);
		return nullptr;
	}

	CachedPakData.Add(Standardized, NewData);
	return NewData;
}

bool FPPakPatcher::CreateDiff(const FString& InPatchFilename, const FString& InNewPakFile, const FString& InOldPakFile, FPResPatchDataPtr& OutPatch,
	EPPakPatchMode InMode /*= EPPakPatchMode::PakAware*/, EPakPatchCompressType InCompressType /*= EPakPatchCompressType::None*/)
{
	FPPakFileDataPtr NewPak = LoadOrGetPakData(InNewPakFile);
	FPPakFileDataPtr OldPak = LoadOrGetPakData(InOldPakFile);
	if (!NewPak.IsValid() || !OldPak.IsValid())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CreateDiff - Failed to load pak. New:%s, Old:%s"), *InNewPakFile, *InOldPakFile);
		return false;
	}
	return CreatePakDiff(InPatchFilename, NewPak, OldPak, OutPatch, InMode, InCompressType);
}

bool FPPakPatcher::PatchDiff(const FString& InNewPakFilename, const FString& InOldPakFile, const FPResPatchDataPtr& InPatch)
{
	FPPakFileDataPtr OldPak = LoadOrGetPakData(InOldPakFile);
	if (!OldPak.IsValid())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::PatchDiff - Failed to load old pak: %s"), *InOldPakFile);
		return false;
	}
	return PatchPak(InNewPakFilename, OldPak, InPatch);
}

bool FPPakPatcher::CheckDiff(const FString& InNewPakFile, const FString& InOldPakFile, const FPResPatchDataPtr& InPatch)
{
	FPPakFileDataPtr NewPak = LoadOrGetPakData(InNewPakFile);
	FPPakFileDataPtr OldPak = LoadOrGetPakData(InOldPakFile);
	if (!NewPak.IsValid() || !OldPak.IsValid())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckDiff - Failed to load pak. New:%s, Old:%s"), *InNewPakFile, *InOldPakFile);
		return false;
	}
	return CheckPakDiff(NewPak, OldPak, InPatch);
}

// ---------------------------------------------------------------------------
// CreatePakDiff
// ---------------------------------------------------------------------------

bool FPPakPatcher::CreatePakDiff(const FString& InPatchFilename, const FPPakFileDataPtr& InNewPak, const FPPakFileDataPtr& InOldPak, FPResPatchDataPtr& OutPatch,
	EPPakPatchMode InMode /*= EPPakPatchMode::PakAware*/, EPakPatchCompressType InCompressType /*= EPakPatchCompressType::None*/)
{
	/*
		pak v10+ layout:
			-> [padding + file entry + data] x n
			-> primary index
			-> path hash index        (optional)
			-> full directory index   (optional)
			-> head (FPakInfo footer)
	*/

	if (InMode == EPPakPatchMode::Binary)
	{
		UE_LOG(LogPPakPacher, Warning,
			TEXT("FPPakPatcher::CreatePakDiff - Binary mode is not implemented yet, fallback to PakAware. Filename:%s"),
			*InPatchFilename);
	}

	if (InCompressType != EPakPatchCompressType::None)
	{
		UE_LOG(LogPPakPacher, Warning,
			TEXT("FPPakPatcher::CreatePakDiff - InCompressType=%d is not propagated to BinPatcher yet, fallback to None. Filename:%s"),
			(int32)InCompressType, *InPatchFilename);
	}

	// PakAware entry 预处理策略
	const EPPakAwarePreprocess Preprocess = UPPakPatcherSettings::Get().PakAwarePreprocess;
	if (Preprocess == EPPakAwarePreprocess::DecryptAndDecompress)
	{
		UE_LOG(LogPPakPacher, Warning,
			TEXT("FPPakPatcher::CreatePakDiff - PakAwarePreprocess=DecryptAndDecompress not implemented yet; fallback to NoDecrypt. Filename:%s"),
			*InPatchFilename);
	}
	const bool bDecrypt = (Preprocess == EPPakAwarePreprocess::DecryptAndCompress);

	const double StartTime = FPlatformTime::Seconds();

	OutPatch = MakeShareable(new FPResPatchData());

	// 构建侧：始终同时计算 MD5 + CRC32，运行时按 CheckFileHashType 选择性校验
	const uint32 OldPakCrc32 = FPPakPatcherUtils::CalculateFileCrc32(InOldPak->PakFilename);
	const uint32 NewPakCrc32 = FPPakPatcherUtils::CalculateFileCrc32(InNewPak->PakFilename);

	OutPatch->BeginRecord(InPatchFilename, EPResPatchType::Pak,
		FPaths::GetCleanFilename(InOldPak->PakFilename),
		FPaths::GetCleanFilename(InNewPak->PakFilename),
		InOldPak->GetPakFileMD5(), InNewPak->GetPakFileMD5(),
		OldPakCrc32, NewPakCrc32,
		InCompressType);

	// 写入 PakAware 预处理策略到 Header（与 Settings 保持一致；Patch 阶段必须按相同策略反向重组）
	OutPatch->Header.PakAwarePreprocess = Preprocess;

	// Header 元数据：pak 版本、文件大小、生成模式、主加密 key Guid（解密失败时定位用）
	OutPatch->Header.OldVersion = InOldPak->PakInfo.Version;
	OutPatch->Header.NewVersion = InNewPak->PakInfo.Version;
	OutPatch->Header.OldSize    = FPPakPatcherUtils::GetFileSize(InOldPak->PakFilename);
	OutPatch->Header.NewSize    = FPPakPatcherUtils::GetFileSize(InNewPak->PakFilename);
	OutPatch->Header.GenerateMode = InMode;
	OutPatch->Header.PrincipalEncryptionKeyGuid = InNewPak->PakInfo.EncryptionKeyGuid;

	// Pak body 专属字段初始化
	check(OutPatch->GetPakBody() != nullptr);
	OutPatch->GetPakBody()->bSign = InNewPak->bIsSigned;
	OutPatch->GetPakBody()->MountPoint = InNewPak->PakFilePtr->GetMountPoint();

	FPakFile& NewPakFile = *InNewPak->PakFilePtr;
	FSharedPakReader NewPakReader = NewPakFile.GetSharedReader(NULL);
	FArchive& NewPakArchive = NewPakReader.GetArchive();

	FPakFile& OldPakFile = *InOldPak->PakFilePtr;
	FSharedPakReader OldPakReader = OldPakFile.GetSharedReader(NULL);
	FArchive& OldPakArchive = OldPakReader.GetArchive();

	IPBinPatcher* BinPatcher = IPPakPatcherModule::Get().GetBinPatcher();

	FKeyChain& KeyChain = FPPakPatcherKeyChainHelper::Get().GetKeyChain();

	// DecryptAndCompress：查找加密 key
	const FAES::FAESKey* EncKey = nullptr;
	if (bDecrypt)
	{
		EncKey = FindEncryptionKey(KeyChain, InNewPak->PakInfo.EncryptionKeyGuid);
		if (!EncKey)
		{
			UE_LOG(LogPPakPacher, Error,
				TEXT("FPPakPatcher::CreatePakDiff - DecryptAndCompress requires encryption key but none found for Guid=%s. Filename:%s"),
				*InNewPak->PakInfo.EncryptionKeyGuid.ToString(), *InPatchFilename);
			return false;
		}
		UE_LOG(LogPPakPacher, Display,
			TEXT("FPPakPatcher::CreatePakDiff - DecryptAndCompress mode: entry data will be decrypted before diff."));
	}

	struct FPPakEntryInfo
	{
		FString Filename;
		FPakEntry Info;
	};

	TArray<FPPakEntryInfo> EntryInfos;
	for (FPakFile::FPakEntryIterator It(NewPakFile); It; ++It)
	{
		// FPakEntryIterator 默认已跳过 delete record，这里再做一次显式 guard。
		if (It.Info().IsDeleteRecord() || It.Info().Offset < 0)
		{
			continue;
		}
		FPPakEntryInfo& Pair = EntryInfos.AddZeroed_GetRef();
		Pair.Filename = *It.TryGetFilename();
		Pair.Info = It.Info();
	}

	// Reorder to serialization order.
	EntryInfos.Sort([](const FPPakEntryInfo& A, const FPPakEntryInfo& B) {
		return A.Info.Offset < B.Info.Offset;
		});

	for (int32 i = 0; i < EntryInfos.Num(); ++i)
	{
		const FString& FileName = EntryInfos[i].Filename;
		const FPakEntry& NewEntry = EntryInfos[i].Info;
		const int64 NewRealSize = CalcEntryRealSize(NewEntry, NewPakFile);

		FString NewHash, OldHash;

		NewPakArchive.Seek(NewEntry.Offset);

		if (UPPakPatcherSettings::Get().bDoubleCheckEntry)
		{
			//double check new entry info and move NewPakArchive into place
			FPakEntry NewEntryInfo;
			NewEntryInfo.Serialize(NewPakArchive, NewPakFile.GetInfo().Version);
			if (!NewEntryInfo.IndexDataEquals(NewEntry))
			{
				UE_LOG(LogPPakPacher, Error, TEXT("NewPakEntry double check failed. filename:%s"), *FileName);
				continue;
			}
			NewHash = BytesToHex(NewEntryInfo.Hash, sizeof(NewEntryInfo.Hash));
		}

		//see if entry exists in old pak							
		FPakEntry OldEntry;
		FPakFile::EFindResult FoundResult = OldPakFile.Find(OldPakFile.GetMountPoint() / FileName, &OldEntry);
		if (FoundResult == FPakFile::EFindResult::Found && !OldEntry.IsDeleteRecord() && OldEntry.Offset >= 0)
		{
			const int64 OldRealSize = CalcEntryRealSize(OldEntry, OldPakFile);
			OldPakArchive.Seek(OldEntry.Offset);
			if (UPPakPatcherSettings::Get().bDoubleCheckEntry)
			{
				//double check old entry info and move OldPakReader into place
				FPakEntry OldEntryInfo;
				OldEntryInfo.Serialize(OldPakArchive, OldPakFile.GetInfo().Version);
				if (!OldEntryInfo.IndexDataEquals(OldEntry))
				{
					UE_LOG(LogPPakPacher, Error, TEXT("OldPakEntry double check failed. filename:%s"), *FileName);
					continue;
				}
				OldHash = BytesToHex(OldEntryInfo.Hash, sizeof(OldEntryInfo.Hash));
			}

			FPPakMemoryArchive NewPakWriter(NewRealSize);
			NewPakArchive.Seek(NewEntry.Offset);
			NewPakArchive.Serialize(NewPakWriter.GetData(), NewRealSize);

			FPPakMemoryArchive OldPakWriter(OldRealSize);
			OldPakArchive.Seek(OldEntry.Offset);
			OldPakArchive.Serialize(OldPakWriter.GetData(), OldRealSize);

			// DecryptAndCompress：在 diff 前解密 entry data（得到压缩态字节），减小 diff 体积
			if (bDecrypt && EncKey)
			{
				DecryptEntryData(NewPakWriter.GetData(), NewRealSize, NewEntry, NewPakFile, *EncKey);
				DecryptEntryData(OldPakWriter.GetData(), OldRealSize, OldEntry, OldPakFile, *EncKey);
			}

			if (NewHash == OldHash &&
				NewRealSize == OldRealSize &&
				FMemory::Memcmp(NewPakWriter.GetData(), OldPakWriter.GetData(), NewRealSize) == 0)
			{
				//record keep same
				OutPatch->RecordKeep(FileName, NewPakFile, OldPakFile, NewEntry, OldEntry, NewRealSize, OldRealSize);
			}
			else
			{
				//record modified
				TArray<uint8> PatchData;
				BinPatcher->CreateDiff(NewPakWriter.GetData(), NewPakWriter.GetSize(), OldPakWriter.GetData(), OldPakWriter.GetSize(), PatchData);
				bool bCheckSuccess = BinPatcher->CheckDiff(NewPakWriter.GetData(), NewPakWriter.GetSize(), OldPakWriter.GetData(), OldPakWriter.GetSize(), PatchData.GetData(), PatchData.Num());
				check(bCheckSuccess);
				FPPakFilePatchInfo& PatchInfo = OutPatch->RecordModify(FileName, NewPakFile, OldPakFile, NewEntry, OldEntry, PatchData, NewRealSize, OldRealSize);
			}
		}
		else
		{
			//record new file.
			FPPakMemoryArchive MemWriter(NewRealSize);
			NewPakArchive.Seek(NewEntry.Offset);
			NewPakArchive.Serialize(MemWriter.GetData(), NewRealSize);

			// DecryptAndCompress：新文件也需要解密后存入 patch（运行时回放会重新加密）
			if (bDecrypt && EncKey)
			{
				DecryptEntryData(MemWriter.GetData(), NewRealSize, NewEntry, NewPakFile, *EncKey);
			}

			OutPatch->RecordNew(FileName, NewPakFile, OldPakFile, NewEntry, MemWriter, NewRealSize);
		}
	}

	const FPakInfo& NewPakInfo = NewPakFile.GetInfo();
	const FPakInfo& OldPakInfo = OldPakFile.GetInfo();

	// 解析 new/old pak 的 Secondary Index meta（v10+ 三段布局）
	PPakPatcherPrivate::FSecondaryIndexMeta NewMeta;
	PPakPatcherPrivate::FSecondaryIndexMeta OldMeta;
	if (!PPakPatcherPrivate::LoadSecondaryIndexMeta(NewPakArchive, NewPakInfo, NewMeta))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CreatePakDiff - LoadSecondaryIndexMeta(new) failed. %s"), *InNewPak->PakFilename);
		return false;
	}
	if (!PPakPatcherPrivate::LoadSecondaryIndexMeta(OldPakArchive, OldPakInfo, OldMeta))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CreatePakDiff - LoadSecondaryIndexMeta(old) failed. %s"), *InOldPak->PakFilename);
		return false;
	}

	// 无条件记录 Primary Index 的 SHA1（与 signed writer 是否启用无关；用于 CheckPakDiff 时三段 hash 校验）
	OutPatch->GetPakBody()->IndexHash = NewPakInfo.IndexHash;

	auto RecordDataBlock = [&](FPPakPatchDataInfo& DataInfo, int64 NewOffset, int64 NewSize, int64 OldOffset, int64 OldSize, bool bIsPatchData)
	{
		FPPakMemoryArchive NewMemWriter(NewSize);
		NewPakArchive.Seek(NewOffset);
		NewPakArchive.Serialize(NewMemWriter.GetData(), NewSize);

		if (bIsPatchData && OldSize > 0)
		{
			FPPakMemoryArchive OldMemWriter(OldSize);
			OldPakArchive.Seek(OldOffset);
			OldPakArchive.Serialize(OldMemWriter.GetData(), OldSize);

			TArray<uint8> PatchData;
			BinPatcher->CreateDiff(NewMemWriter.GetData(), NewMemWriter.GetSize(), OldMemWriter.GetData(), OldMemWriter.GetSize(), PatchData);
			bool bCheckSuccess = BinPatcher->CheckDiff(NewMemWriter.GetData(), NewMemWriter.GetSize(), OldMemWriter.GetData(), OldMemWriter.GetSize(), PatchData.GetData(), PatchData.Num());
			check(bCheckSuccess);

			OutPatch->RecordDataBlock(DataInfo, NewOffset, NewSize, OldOffset, OldSize, PatchData.GetData(), PatchData.Num(), bIsPatchData);
		}
		else
		{
			// 旧 pak 没有对应段（OldSize=0），只能按 full data 记录新段。
			OutPatch->RecordDataBlock(DataInfo, NewOffset, NewSize, OldOffset, OldSize, NewMemWriter.GetData(), NewMemWriter.GetSize(), false);
		}
	};

	// [1] Primary Index block
	{
		bool bIsPatchData = UPPakPatcherSettings::Get().bBinaryPatchIndexBlock;

		int64 NewOffset = NewPakInfo.IndexOffset;
		int64 NewSize   = NewPakInfo.IndexSize;
		int64 OldOffset = OldPakInfo.IndexOffset;
		int64 OldSize   = OldPakInfo.IndexSize;

		RecordDataBlock(OutPatch->GetPakBody()->IndexPatchInfo, NewOffset, NewSize, OldOffset, OldSize, bIsPatchData);
	}

	// [2] Path Hash Index block (optional)
	{
		OutPatch->GetPakBody()->bHasPathHashIndex = NewMeta.bHasPathHashIndex;
		OutPatch->GetPakBody()->PathHashIndexHash = NewMeta.PathHashIndexHash;

		if (NewMeta.bHasPathHashIndex)
		{
			bool bIsPatchData = UPPakPatcherSettings::Get().bBinaryPatchPathHashBlock;

			int64 NewOffset = NewMeta.PathHashIndexOffset;
			int64 NewSize   = NewMeta.PathHashIndexSize;
			int64 OldOffset = OldMeta.bHasPathHashIndex ? OldMeta.PathHashIndexOffset : 0;
			int64 OldSize   = OldMeta.bHasPathHashIndex ? OldMeta.PathHashIndexSize   : 0;

			// 若 old 不存在对应段，RecordDataBlock 内部会退化成 full data。
			if (!OldMeta.bHasPathHashIndex)
			{
				bIsPatchData = false;
			}
			RecordDataBlock(OutPatch->GetPakBody()->PathHashPatchInfo, NewOffset, NewSize, OldOffset, OldSize, bIsPatchData);
		}
	}

	// [3] Full Directory Index block (optional)
	{
		OutPatch->GetPakBody()->bHasFullDirectoryIndex = NewMeta.bHasFullDirectoryIndex;
		OutPatch->GetPakBody()->FullDirectoryIndexHash = NewMeta.FullDirectoryIndexHash;

		if (NewMeta.bHasFullDirectoryIndex)
		{
			bool bIsPatchData = UPPakPatcherSettings::Get().bBinaryPatchFullDirectoryBlock;

			int64 NewOffset = NewMeta.FullDirectoryIndexOffset;
			int64 NewSize   = NewMeta.FullDirectoryIndexSize;
			int64 OldOffset = OldMeta.bHasFullDirectoryIndex ? OldMeta.FullDirectoryIndexOffset : 0;
			int64 OldSize   = OldMeta.bHasFullDirectoryIndex ? OldMeta.FullDirectoryIndexSize   : 0;

			if (!OldMeta.bHasFullDirectoryIndex)
			{
				bIsPatchData = false;
			}
			RecordDataBlock(OutPatch->GetPakBody()->FullDirectoryPatchInfo, NewOffset, NewSize, OldOffset, OldSize, bIsPatchData);
		}
	}

	// [4] Head block (FPakInfo footer)
	{
		bool bIsPatchData = UPPakPatcherSettings::Get().bBinaryPatchHeadBlock;

		int64 NewSize = NewPakInfo.GetSerializedSize(NewPakInfo.Version);
		int64 NewOffset = NewPakArchive.TotalSize() - NewSize;
		int64 OldSize = OldPakInfo.GetSerializedSize(OldPakInfo.Version);
		int64 OldOffset = OldPakArchive.TotalSize() - OldSize;

		RecordDataBlock(OutPatch->GetPakBody()->HeadPatchInfo, NewOffset, NewSize, OldOffset, OldSize, bIsPatchData);
	}

	// Signature
	if (InNewPak->bIsSigned)
	{
		if (UPPakPatcherSettings::Get().bUseSignWriter)
		{
			// Index hash use for SignedPakWriter
			OutPatch->GetPakBody()->IndexHash = NewPakInfo.IndexHash;
			// TODO : currently missing private key
		}
		if (UPPakPatcherSettings::Get().bRecordSignToPatch)
		{
			FString SignFilename = FPaths::ChangeExtension(InNewPak->PakFilename, TEXT("sig"));
			FArchive* SignFileReader = IFileManager::Get().CreateFileReader(*SignFilename);
			if (SignFileReader)
			{
				int64 Size = SignFileReader->TotalSize();
				FPPakMemoryArchive MemWriter(Size);
				SignFileReader->Seek(0);
				SignFileReader->Serialize(MemWriter.GetData(), Size);

				OutPatch->RecordDataBlock(OutPatch->GetPakBody()->SignFileInfo, 0, Size, 0, 0, MemWriter.GetData(), MemWriter.GetSize(), false);

				SignFileReader->Close();
				delete SignFileReader;
			}
			else
			{
				UE_LOG(LogPPakPacher, Error, TEXT("Create sign file reader failed. filename:%s"), *SignFilename);
			}
		}
	}


	// 处理同伴 IoStore 文件（.utoc / .ucas），把 IoStore body 也写入同一个 FPResPatchData
	if (FPIoStorePatcher::HasIoStoreSibling(InNewPak->PakFilename))
	{
		FPIoStorePatcher IoStorePatcher;
		if (!IoStorePatcher.CreateIoStoreDiff(InNewPak->PakFilename, InOldPak->PakFilename, *OutPatch))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CreatePakDiff - IoStore diff failed for %s"), *InNewPak->PakFilename);
			// 半成品 patch 必须清理：bPrecachePatchDataOnSave=false 时 Writer 已写盘，
			// 不清理会留下"PakBody 完整 + IoStoreBody 缺失"的脏文件。
			OutPatch->AbortRecord();
			OutPatch.Reset();
			return false;
		}
	}

	OutPatch->EndRecord();

	UE_LOG(LogPPakPacher, Display, TEXT("Create pak patch successed. Cost time %.2lfs. Filename:%s"), FPlatformTime::Seconds() - StartTime, *InPatchFilename);

	return true;
}

// ---------------------------------------------------------------------------
// PatchPak
// ---------------------------------------------------------------------------

FArchive* CreatePakPatchWriter(const TCHAR* Filename, const FKeyChain& InKeyChain, bool bSign)
{
	FArchive* Writer = IFileManager::Get().CreateFileWriter(Filename);
	if (Writer)
	{
		if (bSign && UPPakPatcherSettings::Get().bUseSignWriter)
		{
			UE_LOG(LogPPakPacher, Display, TEXT("Creating signed pak %s."), Filename);
			Writer = new FPSignedPakPatchWriter(*Writer, Filename, InKeyChain.GetSigningKey());
		}

		else
		{
			UE_LOG(LogPPakPacher, Display, TEXT("Creating pak %s."), Filename);
		}
	}

	return Writer;
}

bool FPPakPatcher::PatchPak(const FString& InNewPakFilename, const FPPakFileDataPtr& InOldPak, const FPResPatchDataPtr& InPatch)
{
	const double StartTime = FPlatformTime::Seconds();

	FString NewPakFilename = InNewPakFilename;
	FPaths::MakeStandardFilename(NewPakFilename);
	if (NewPakFilename.IsEmpty())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::PatchPak - NewPakFilename was empty."));
		return false;
	}
	if (!InOldPak.IsValid())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::PatchPak - InOldPak was Invalid."));
		return false;
	}

	if (!InPatch.IsValid())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::PatchPak - InPatch was Invalid."));
		return false;
	}

	// 运行时：patch 里记录的 PakAware 预处理策略
	const EPPakAwarePreprocess PatchPreprocess = InPatch->Header.PakAwarePreprocess;
	if (PatchPreprocess == EPPakAwarePreprocess::DecryptAndDecompress)
	{
		UE_LOG(LogPPakPacher, Error,
			TEXT("FPPakPatcher::PatchPak - Patch was generated with PakAwarePreprocess=DecryptAndDecompress which is not implemented. Filename:%s"),
			*InOldPak->PakFilename);
		return false;
	}
	const bool bDecrypt = (PatchPreprocess == EPPakAwarePreprocess::DecryptAndCompress);
	const FAES::FAESKey* EncKey = nullptr;

	// 运行时：根据 CheckFileHashType 决定是否/如何校验 old pak hash
	if (!FPPakPatcherUtils::VerifyFileHashByCheckType(InOldPak->PakFilename,
		InPatch->Header.OldMD5, InPatch->Header.OldCrc32,
		TEXT("FPPakPatcher::PatchPak/OldPak")))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::PatchPak - Old pak hash mismatch. Pak Filename:%s, Record Filename:%s"),
			*InOldPak->PakFilename, *InPatch->Header.OldFileName);
		return false;
	}

	IPBinPatcher* BinPatcher = IPPakPatcherModule::Get().GetBinPatcher();
	FPakFile& OldPakFile = *InOldPak->PakFilePtr;
	FSharedPakReader OldPakReader = OldPakFile.GetSharedReader(NULL);
	FArchive& OldPakArchive = OldPakReader.GetArchive();

	FKeyChain& KeyChain = FPPakPatcherKeyChainHelper::Get().GetKeyChain();

	// DecryptAndCompress：查找加密 key（在 KeyChain 声明后）
	if (bDecrypt)
	{
		EncKey = FindEncryptionKey(KeyChain, InPatch->Header.PrincipalEncryptionKeyGuid);
		if (!EncKey)
		{
			UE_LOG(LogPPakPacher, Error,
				TEXT("FPPakPatcher::PatchPak - DecryptAndCompress requires encryption key but none found. Filename:%s"),
				*InOldPak->PakFilename);
			return false;
		}
	}

	bool bSign = InPatch->GetPakBody()->bSign;
	TUniquePtr<FArchive> Writer(CreatePakPatchWriter(*NewPakFilename, KeyChain, bSign));

	if (!Writer)
	{
		UE_LOG(LogPPakPacher, Error, TEXT("Unable to create pak file \"%s\"."), *NewPakFilename);
		return false;
	}

	auto Write = [&](uint8* InData, int64 InSize)
	{
		Writer->Serialize(InData, InSize);
	};


	int64 PaddingBufferSize = 64 * 1024;
	FPPakPatcherMemory PaddingBuffer;
	auto SerializePaddingToOffset = [&](int64 InOffset){
		if (PaddingBuffer.GetSize() == 0)
		{
			PaddingBuffer.Resize(PaddingBufferSize);
			FMemory::Memset(PaddingBuffer.GetData(), 0, PaddingBufferSize);
		}
		int64 PaddingSize = InOffset - Writer->TotalSize();
		check(PaddingSize >= 0);
		check(PaddingSize <= PaddingBufferSize);
		if (PaddingSize > 0)
		{
			Write(PaddingBuffer.GetData(), PaddingSize);
		}
	};

	for(int32 i=0; i< InPatch->GetPakBody()->FilePatchInfos.Num(); ++i)
	{
		FPPakFilePatchInfo& FilePatchInfo = InPatch->GetPakBody()->FilePatchInfos[i];

		check(FilePatchInfo.DataInfo.NewOffset >= Writer->Tell());

		SerializePaddingToOffset(FilePatchInfo.DataInfo.NewOffset);
		FString Filename = FilePatchInfo.FileName;
		if (FilePatchInfo.PatchType == EPakFilePatchType::Keep || FilePatchInfo.PatchType == EPakFilePatchType::Modify)
		{
			//see if entry exists in old pak							
			FPakEntry OldEntry;
			FString OldHash;
			FPakFile::EFindResult FoundResult = OldPakFile.Find(OldPakFile.GetMountPoint() / FilePatchInfo.FileName, &OldEntry);
			if (FoundResult == FPakFile::EFindResult::Found)
			{
				OldPakArchive.Seek(OldEntry.Offset);
				if (UPPakPatcherSettings::Get().bDoubleCheckEntry)
				{
					//double check old entry info and move OldPakReader into place
					FPakEntry OldEntryInfo;
					OldEntryInfo.Serialize(OldPakArchive, OldPakFile.GetInfo().Version);
					if (!OldEntryInfo.IndexDataEquals(OldEntry))
					{
						UE_LOG(LogPPakPacher, Error, TEXT("OldPakEntry double check failed. filename:%s"), *Filename);
						continue;
					}
					OldHash = BytesToHex(OldEntryInfo.Hash, sizeof(OldEntryInfo.Hash));
				}
			}
			else
			{
				UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::PatchPak - Can't find pak file:[%s], in pak:[%s]"), *Filename, *InOldPak->PakFilename);
				return false;
			}

			int64 OldRealSize = FilePatchInfo.OldFileRealSize;
			FPPakMemoryArchive OldPakMemory(OldRealSize);
			OldPakArchive.Seek(FilePatchInfo.DataInfo.OldOffset);
			OldPakArchive.Serialize(OldPakMemory.GetData(), OldRealSize);

			if (FilePatchInfo.PatchType == EPakFilePatchType::Keep)
			{
				Write(OldPakMemory.GetData(), OldPakMemory.GetSize()); 
				continue;
			}
			else if (FilePatchInfo.PatchType == EPakFilePatchType::Modify)
			{
				// DecryptAndCompress：patch 数据是在解密后的压缩字节上生成的。
				// 运行时需要先解密 Old → patch 得到 New（压缩态）→ 重新加密写出。
				if (bDecrypt && EncKey)
				{
					DecryptEntryData(OldPakMemory.GetData(), OldRealSize, OldEntry, OldPakFile, *EncKey);
				}

				FPPakMemoryArchive NewPakMemory(FilePatchInfo.FileRealSize);
				uint8* PatchData = InPatch->GetFilePatchData(FilePatchInfo.DataInfo);
				BinPatcher->Patch(NewPakMemory.GetData(), NewPakMemory.GetSize(),
					OldPakMemory.GetData(), OldPakMemory.GetSize(),
					PatchData, FilePatchInfo.DataInfo.DataSize);

				// 重新加密 New entry data
				// 用 FilePatchInfo.Entry（构建时保存的 NewEntry）和 NewVersion 计算正确的 header 大小
				if (bDecrypt && EncKey)
				{
					EncryptEntryData(NewPakMemory.GetData(), NewPakMemory.GetSize(),
						FilePatchInfo.Entry, InPatch->Header.NewVersion, *EncKey);
				}

				Write(NewPakMemory.GetData(), NewPakMemory.GetSize());
				continue;
			}
		}
		else if (FilePatchInfo.PatchType == EPakFilePatchType::New)
		{
			uint8* PatchData = InPatch->GetFilePatchData(FilePatchInfo.DataInfo);
			// DecryptAndCompress：New entry 的 patch 数据是解密态，需要重新加密后写出
			if (bDecrypt && EncKey && FilePatchInfo.Entry.IsEncrypted())
			{
				// PatchData 指向 patch 内部缓冲区，不能就地修改——拷贝一份后加密再写出
				FPPakMemoryArchive NewEntryMem(FilePatchInfo.DataInfo.DataSize);
				FMemory::Memcpy(NewEntryMem.GetData(), PatchData, FilePatchInfo.DataInfo.DataSize);
				EncryptEntryData(NewEntryMem.GetData(), NewEntryMem.GetSize(),
					FilePatchInfo.Entry, InPatch->Header.NewVersion, *EncKey);
				Write(NewEntryMem.GetData(), NewEntryMem.GetSize());
			}
			else
			{
				Write(PatchData, FilePatchInfo.DataInfo.DataSize);
			}
		}
		else if (FilePatchInfo.PatchType == EPakFilePatchType::Delete)
		{
			//nothing to do.
		}
	}

	auto PatchDataBlock = [&](FPPakPatchDataInfo& Info)
	{
		if (Info.NewSize <= 0)
		{
			return; // block 不存在（例如 bHasPathHashIndex=false）
		}
		SerializePaddingToOffset(Info.NewOffset);
		uint8* PatchData = InPatch->GetFilePatchData(Info);
		if (Info.bIsPatchData)
		{
			FPPakMemoryArchive OldPakMemory(Info.OldSize);
			OldPakArchive.Seek(Info.OldOffset);
			OldPakArchive.Serialize(OldPakMemory.GetData(), Info.OldSize);

			FPPakMemoryArchive NewPakMemory(Info.NewSize);

			BinPatcher->Patch(NewPakMemory.GetData(), NewPakMemory.GetSize(),
				OldPakMemory.GetData(), OldPakMemory.GetSize(),
				PatchData, Info.DataSize);

			Writer->Serialize(NewPakMemory.GetData(), NewPakMemory.GetSize());
		}
		else
		{
			Writer->Serialize(PatchData, Info.DataSize);
		}
	};

	// [1] Primary Index
	PatchDataBlock(InPatch->GetPakBody()->IndexPatchInfo);

	// [2] Path Hash Index (optional)
	if (InPatch->GetPakBody()->bHasPathHashIndex)
	{
		PatchDataBlock(InPatch->GetPakBody()->PathHashPatchInfo);
	}

	// [3] Full Directory Index (optional)
	if (InPatch->GetPakBody()->bHasFullDirectoryIndex)
	{
		PatchDataBlock(InPatch->GetPakBody()->FullDirectoryPatchInfo);
	}

	// [4] Head (FPakInfo footer)
	PatchDataBlock(InPatch->GetPakBody()->HeadPatchInfo);

	if (bSign)
	{
		if (UPPakPatcherSettings::Get().bUseSignWriter)
		{
			TArray<uint8> SignatureData;
			SignatureData.Append(InPatch->GetPakBody()->IndexHash.Hash, UE_ARRAY_COUNT(FSHAHash::Hash));
			((FPSignedPakPatchWriter*)Writer.Get())->SetSignatureData(SignatureData);
		}
		if (UPPakPatcherSettings::Get().bRecordSignToPatch)
		{
			FPPakPatchDataInfo& Info = InPatch->GetPakBody()->SignFileInfo;
			uint8* PatchData = InPatch->GetFilePatchData(Info);
			FString SignFilename = FPaths::ChangeExtension(NewPakFilename, UPPakPatcherSettings::Get().NewSignExtension);
			FArchive* SignFileWriter = IFileManager::Get().CreateFileWriter(*SignFilename);
			if (SignFileWriter)
			{
				SignFileWriter->Serialize(PatchData, Info.DataSize);
				SignFileWriter->Close();
				delete SignFileWriter;
			}
			else
			{
				UE_LOG(LogPPakPacher, Error, TEXT("Create sign file writer failed. filename:%s"), *SignFilename);
			}
		}
	}

	Writer->Close();

	// 同伴 IoStore 文件（.utoc / .ucas）回放
	if (InPatch->GetIoStoreBody() != nullptr)
	{
		FPIoStorePatcher IoStorePatcher;
		if (!IoStorePatcher.PatchIoStore(InNewPakFilename, InOldPak->PakFilename, *InPatch))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::PatchPak - IoStore patch failed for %s"), *InNewPakFilename);
			return false;
		}
	}

	UE_LOG(LogPPakPacher, Display, TEXT("Patch pak successed. Cost time %.2lfs. Filename:%s"), FPlatformTime::Seconds() - StartTime, *InNewPakFilename);

	return true;
}


// ---------------------------------------------------------------------------
// CheckPakDiff
// ---------------------------------------------------------------------------

bool FPPakPatcher::CheckPakDiff(const FPPakFileDataPtr& InNewPak, const FPPakFileDataPtr& InOldPak, const FPResPatchDataPtr& InPatch)
{
	const double StartTime = FPlatformTime::Seconds();

	FPakFile& NewPakFile = *InNewPak->PakFilePtr;
	FSharedPakReader NewPakReader = NewPakFile.GetSharedReader(NULL);
	FArchive& NewPakArchive = NewPakReader.GetArchive();

	FPakFile& OldPakFile = *InOldPak->PakFilePtr;
	FSharedPakReader OldPakReader = OldPakFile.GetSharedReader(NULL);
	FArchive& OldPakArchive = OldPakReader.GetArchive();

	IPBinPatcher* BinPatcher = IPPakPatcherModule::Get().GetBinPatcher();

	int64 PaddingBufferSize = 64 * 1024;
	FPPakPatcherMemory PaddingBuffer;
	auto CheckReaderPaddingToOffset = [&](FArchive& Reader, int64 InToOffset) -> bool
		{
			if (bool bCheckReaderPadding = false)
			{
				int64 Size = InToOffset - Reader.Tell();
				if (Size == 0)
				{
					return true;
				}
				if (NewPakArchive.Tell() + Size > Reader.TotalSize())
				{
					return false;
				}
				if (PaddingBuffer.GetSize() == 0)
				{
					PaddingBuffer.Resize(PaddingBufferSize);
					FMemory::Memset(PaddingBuffer.GetData(), 0, PaddingBufferSize);
				}
				FPPakPatcherMemory Memory;
				Memory.Resize(Size);
				Reader.Serialize(Memory.GetData(), Size);
				return FMemory::Memcmp(Memory.GetData(), PaddingBuffer.GetData(), Size) == 0;
			}
			return true;
		};

	FKeyChain& KeyChain = FPPakPatcherKeyChainHelper::Get().GetKeyChain();

	NewPakArchive.Seek(0);

	for (FPPakFilePatchInfo& FilePatchInfo : InPatch->GetPakBody()->FilePatchInfos)
	{
		// check padding.
		int64 StartOffset = NewPakArchive.Tell();

		// debug.
		{
			NewPakArchive.Seek(StartOffset);
		}
		if (!CheckReaderPaddingToOffset(NewPakArchive, FilePatchInfo.DataInfo.NewOffset))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Check PakFile Padding Offset Failed. Offset:%lld, Size:%lld"), StartOffset, FilePatchInfo.DataInfo.NewOffset - StartOffset);
			return false;
		}

		const FString& Filename = FilePatchInfo.FileName;

		//see if entry exists in new pak							
		FPakEntry NewEntry;
		FString NewHash;
		{
			FPakFile::EFindResult FoundResult = NewPakFile.Find(NewPakFile.GetMountPoint() / Filename, &NewEntry);
			if (FoundResult == FPakFile::EFindResult::Found)
			{
				NewPakArchive.Seek(NewEntry.Offset);
				if (UPPakPatcherSettings::Get().bDoubleCheckEntry)
				{
					//double check new entry info and move NewPakReader into place
					FPakEntry _NewEntry;
					_NewEntry.Serialize(NewPakArchive, NewPakFile.GetInfo().Version);
					if (!_NewEntry.IndexDataEquals(NewEntry))
					{
						UE_LOG(LogPPakPacher, Error, TEXT("NewPakEntry double check failed. filename:%s, PakFilename:%s"), *Filename, *InNewPak->PakFilename);
						return false;
					}
					NewHash = BytesToHex(_NewEntry.Hash, sizeof(_NewEntry.Hash));
				}
			}
			else
			{
				UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Fild new pak file failed. filename:%s, PakFilename:%s"), *Filename, *InNewPak->PakFilename);
				return false;
			}
		}

		int64 NewRealSize = FilePatchInfo.FileRealSize;
		FPPakMemoryArchive NewPakMemory(NewRealSize);
		NewPakArchive.Seek(NewEntry.Offset);
		NewPakArchive.Serialize(NewPakMemory.GetData(), NewRealSize);

		if (FilePatchInfo.PatchType == EPakFilePatchType::Keep || FilePatchInfo.PatchType == EPakFilePatchType::Modify)
		{
			int64 OldRealSize = FilePatchInfo.OldFileRealSize;
			FPPakMemoryArchive OldPakMemory(OldRealSize);
			OldPakArchive.Seek(FilePatchInfo.DataInfo.OldOffset);
			OldPakArchive.Serialize(OldPakMemory.GetData(), OldRealSize);

			if (FilePatchInfo.PatchType == EPakFilePatchType::Keep)
			{
				if (NewPakMemory != OldPakMemory)
				{
					UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Compare pak file memory failed. filename:%s, PatchType:%s"),
						*Filename, *FPPakPatcherEnumHelper::ToString(FilePatchInfo.PatchType));
					return false;
				}
				continue;
			}
			else if (FilePatchInfo.PatchType == EPakFilePatchType::Modify)
			{
				FPPakMemoryArchive NewPakMemory2(NewRealSize);

				uint8* PatchData = InPatch->GetFilePatchData(FilePatchInfo.DataInfo);

				BinPatcher->Patch(NewPakMemory2.GetData(), NewPakMemory2.GetSize(),
					OldPakMemory.GetData(), OldPakMemory.GetSize(),
					PatchData, FilePatchInfo.DataInfo.DataSize);

				if (NewPakMemory != NewPakMemory2)
				{
					UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Compare pak file memory failed. filename:%s, PatchType:%s"),
						*Filename, *FPPakPatcherEnumHelper::ToString(FilePatchInfo.PatchType));
					return false;
				}
				continue;
			}
		}
		else if (FilePatchInfo.PatchType == EPakFilePatchType::New)
		{
			if (FilePatchInfo.DataInfo.DataSize != NewPakMemory.GetSize())
			{
				UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Compare pak file memory size not equal. filename:%s, PatchType:%s, NewSize:%lld, RecordSize:%lld"),
					*Filename, *FPPakPatcherEnumHelper::ToString(FilePatchInfo.PatchType), NewPakMemory.GetSize(), FilePatchInfo.DataInfo.DataSize);
				return false;	
			}
			uint8* PatchData = InPatch->GetFilePatchData(FilePatchInfo.DataInfo);
			if (FMemory::Memcmp(PatchData, NewPakMemory.GetData(), FilePatchInfo.DataInfo.DataSize) != 0)
			{
				UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Compare pak file memory failed. filename:%s, PatchType:%s"),
					*Filename, *FPPakPatcherEnumHelper::ToString(FilePatchInfo.PatchType));
				return false;
			}
			continue;
		}
	}

	const FPakInfo& NewPakInfoForCheck = NewPakFile.GetInfo();

	auto CheckPatchData = [&](FPPakPatchDataInfo& Info, const FSHAHash* ExpectedHash, const TCHAR* BlockName) -> bool
	{
		if (Info.NewSize <= 0)
		{
			return true; // block 不存在
		}
		uint8* PatchData = InPatch->GetFilePatchData(Info);
		FPPakMemoryArchive NewPakMemory(Info.NewSize);
		NewPakArchive.Seek(Info.NewOffset);
		NewPakArchive.Serialize(NewPakMemory.GetData(), Info.NewSize);

		if (Info.bIsPatchData)
		{
			FPPakMemoryArchive OldPakMemory(Info.OldSize);
			OldPakArchive.Seek(Info.OldOffset);
			OldPakArchive.Serialize(OldPakMemory.GetData(), Info.OldSize);

			FPPakMemoryArchive PatchedMemory(Info.NewSize);
			BinPatcher->Patch(PatchedMemory.GetData(), PatchedMemory.GetSize(),
				OldPakMemory.GetData(), OldPakMemory.GetSize(),
				PatchData, Info.DataSize);

			if (NewPakMemory != PatchedMemory)
			{
				UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - [%s] Patched block memory compare failed."), BlockName);
				return false;
			}

			// Hash 校验：对回放后的字节（密文）解密再 SHA1，与期望 hash 比对。
			if (ExpectedHash != nullptr)
			{
				TArray<uint8> HashCopy;
				HashCopy.Append(PatchedMemory.GetData(), PatchedMemory.GetSize());
				if (!PPakPatcherPrivate::DecryptAndValidateIndexBytes(NewPakInfoForCheck, HashCopy, *ExpectedHash))
				{
					UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - [%s] SHA1 hash mismatch after patch replay."), BlockName);
					return false;
				}
			}
		}
		else
		{
			if (FMemory::Memcmp(PatchData, NewPakMemory.GetData(), Info.NewSize) != 0)
			{
				UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - [%s] block memory compare failed."), BlockName);
				return false;
			}

			// Hash 校验（full data 情况）
			if (ExpectedHash != nullptr)
			{
				TArray<uint8> HashCopy;
				HashCopy.Append(PatchData, Info.NewSize);
				if (!PPakPatcherPrivate::DecryptAndValidateIndexBytes(NewPakInfoForCheck, HashCopy, *ExpectedHash))
				{
					UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - [%s] SHA1 hash mismatch on full data."), BlockName);
					return false;
				}
			}
		}
		return true;
	};

	// 解析 new pak 的 secondary meta，和 patch 里的期望对齐
	PPakPatcherPrivate::FSecondaryIndexMeta NewMeta;
	if (!PPakPatcherPrivate::LoadSecondaryIndexMeta(NewPakArchive, NewPakFile.GetInfo(), NewMeta))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - LoadSecondaryIndexMeta(new) failed."));
		return false;
	}

	// [1] Primary Index
	{
		FPPakPatchDataInfo& Info = InPatch->GetPakBody()->IndexPatchInfo;
		int64 StartOffset = NewPakArchive.Tell();
		if (!CheckReaderPaddingToOffset(NewPakArchive, Info.NewOffset))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Check pak primary index block padding failed. Offset:%lld, Size:%lld"), StartOffset, Info.NewOffset - StartOffset);
			return false;
		}
		const FPakInfo& NewPakInfo = NewPakFile.GetInfo();
		if (NewPakInfo.IndexOffset != Info.NewOffset || NewPakInfo.IndexSize != Info.NewSize)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Primary Index offset/size mismatch. Pak:%lld/%lld, Patch:%lld/%lld"),
				NewPakInfo.IndexOffset, NewPakInfo.IndexSize, Info.NewOffset, Info.NewSize);
			return false;
		}
		// Patch 里记录的 IndexHash 必须与当前 new pak 的 Info.IndexHash 一致
		if (InPatch->GetPakBody()->IndexHash != NewPakInfo.IndexHash)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Primary Index hash mismatch between patch record and new pak."));
			return false;
		}
		if (!CheckPatchData(Info, &NewPakInfo.IndexHash, TEXT("PrimaryIndex")))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Primary Index block CheckPatchData failed."));
			return false;
		}
	}

	// [2] Path Hash Index
	if (InPatch->GetPakBody()->bHasPathHashIndex)
	{
		if (!NewMeta.bHasPathHashIndex)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Patch expects PathHashIndex but new pak has none."));
			return false;
		}
		FPPakPatchDataInfo& Info = InPatch->GetPakBody()->PathHashPatchInfo;
		if (NewMeta.PathHashIndexOffset != Info.NewOffset || NewMeta.PathHashIndexSize != Info.NewSize)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - PathHashIndex offset/size mismatch. Pak:%lld/%lld, Patch:%lld/%lld"),
				NewMeta.PathHashIndexOffset, NewMeta.PathHashIndexSize, Info.NewOffset, Info.NewSize);
			return false;
		}
		if (InPatch->GetPakBody()->PathHashIndexHash != NewMeta.PathHashIndexHash)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - PathHashIndex hash mismatch between patch record and new pak."));
			return false;
		}
		if (!CheckPatchData(Info, &NewMeta.PathHashIndexHash, TEXT("PathHashIndex")))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - PathHashIndex CheckPatchData failed."));
			return false;
		}
	}

	// [3] Full Directory Index
	if (InPatch->GetPakBody()->bHasFullDirectoryIndex)
	{
		if (!NewMeta.bHasFullDirectoryIndex)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Patch expects FullDirectoryIndex but new pak has none."));
			return false;
		}
		FPPakPatchDataInfo& Info = InPatch->GetPakBody()->FullDirectoryPatchInfo;
		if (NewMeta.FullDirectoryIndexOffset != Info.NewOffset || NewMeta.FullDirectoryIndexSize != Info.NewSize)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - FullDirectoryIndex offset/size mismatch. Pak:%lld/%lld, Patch:%lld/%lld"),
				NewMeta.FullDirectoryIndexOffset, NewMeta.FullDirectoryIndexSize, Info.NewOffset, Info.NewSize);
			return false;
		}
		if (InPatch->GetPakBody()->FullDirectoryIndexHash != NewMeta.FullDirectoryIndexHash)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - FullDirectoryIndex hash mismatch between patch record and new pak."));
			return false;
		}
		if (!CheckPatchData(Info, &NewMeta.FullDirectoryIndexHash, TEXT("FullDirectoryIndex")))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - FullDirectoryIndex CheckPatchData failed."));
			return false;
		}
	}

	// [4] Head
	{
		FPPakPatchDataInfo& Info = InPatch->GetPakBody()->HeadPatchInfo;
		const FPakInfo& NewPakInfo = NewPakFile.GetInfo();
		int64 Size = NewPakInfo.GetSerializedSize(NewPakInfo.Version);
		int64 Offset = NewPakArchive.TotalSize() - Size;

		if (Offset != Info.NewOffset || Size != Info.NewSize)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Head block offset/size mismatch. Pak:%lld/%lld, Patch:%lld/%lld"),
				Offset, Size, Info.NewOffset, Info.NewSize);
			return false;
		}

		// Head 块（FPakInfo footer）本身不在加密/hash 范围内，跳过 hash 校验。
		if (!CheckPatchData(Info, nullptr, TEXT("Head")))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Head block CheckPatchData failed."));
			return false;
		}
	}

	// 同伴 IoStore 文件（.utoc / .ucas）校验
	if (InPatch->GetIoStoreBody() != nullptr)
	{
		FPIoStorePatcher IoStorePatcher;
		if (!IoStorePatcher.CheckIoStoreDiff(InNewPak->PakFilename, InOldPak->PakFilename, *InPatch))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - IoStore check failed for %s"), *InNewPak->PakFilename);
			return false;
		}
	}

	UE_LOG(LogPPakPacher, Display, TEXT("Check pak patch successed. Cost time %.2lfs. Filename:%s"), FPlatformTime::Seconds() - StartTime, *InNewPak->PakFilename);

	return true;
}
