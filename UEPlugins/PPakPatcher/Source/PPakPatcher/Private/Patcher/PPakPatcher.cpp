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
#include "Misc/Compression.h"
#include "Misc/ICompressionFormat.h"
#include "Misc/KeyChainUtilities.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Utils/PPakPatcherPerfReport.h"

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
 * 判定 FPakEntry::Hash[20] 是否全零。UnrealPak 若未启用 SHA 计算则 Hash 可能保持初始化的 0 值，
 * 此时不能用 Hash 做等价判定，需 fallback 到 Data 字节比较。
 */
static bool IsAllZeroHash(const uint8 (&Hash)[20])
{
	for (int32 i = 0; i < 20; ++i)
	{
		if (Hash[i] != 0) return false;
	}
	return true;
}

/**
 * 比较两个 entry 的 payload 是否相等（修复 #27）。
 *
 * 背景：
 *   pak 中 entry 物理布局是 [EntryHeader][Data]，EntryHeader 内含 Offset/Size/Hash/
 *   CompressionBlocks[i].CompressedStart/End 等字段，其中 Offset/CompressedStart/CompressedEnd
 *   是该 entry 在物理 pak 文件中的绝对偏移。即使两个 pak 中 entry 数据完全相同，
 *   只要它在 New / Old pak 中位置不同，EntryHeader 字节就会不同。
 *
 *   旧代码用整 buffer memcmp（含 Header）→ 内容相同的 entry 也会因 Header 字段差异
 *   被误判 Modify → 白白把没变化的 entry 走 HDiff，浪费 patch 体积。
 *
 * 修复策略（双重检查）：
 *   1) 优先比较 FPakEntry::Hash[20]（SHA1 of UncompressedData，与 entry 物理位置无关）
 *      - 两边 Hash 都非全零：用 Hash 比较，最精确
 *      - 任一边 Hash 全零（UnrealPak 未启用 SHA）：fallback
 *   2) Fallback：跳过两边各自 EntryHeader，memcmp Data 区（含加密填充字节；DAC 模式下两边解密
 *      后明文相同则填充字节也相同）
 */
static bool IsEntryPayloadEqual(const uint8* NewBuf, const uint8* OldBuf,
	const FPakEntry& NewEntry, const FPakEntry& OldEntry,
	const FPakFile& NewPakFile, const FPakFile& OldPakFile)
{
	const bool bNewHashValid = !IsAllZeroHash(NewEntry.Hash);
	const bool bOldHashValid = !IsAllZeroHash(OldEntry.Hash);
	if (bNewHashValid && bOldHashValid)
	{
		return FMemory::Memcmp(NewEntry.Hash, OldEntry.Hash, 20) == 0;
	}

	// Fallback：跳过 EntryHeader 比较 Data 区
	const int64 NewHeaderSize = NewEntry.GetSerializedSize(NewPakFile.GetInfo().Version);
	const int64 OldHeaderSize = OldEntry.GetSerializedSize(OldPakFile.GetInfo().Version);
	const int64 NewRealSize = CalcEntryRealSize(NewEntry, NewPakFile);
	const int64 OldRealSize = CalcEntryRealSize(OldEntry, OldPakFile);

	const int64 NewDataSize = NewRealSize - NewHeaderSize;
	const int64 OldDataSize = OldRealSize - OldHeaderSize;
	if (NewDataSize != OldDataSize)
	{
		return false;
	}
	if (NewDataSize <= 0)
	{
		return true;
	}
	return FMemory::Memcmp(NewBuf + NewHeaderSize, OldBuf + OldHeaderSize, NewDataSize) == 0;
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
		UE_LOG(LogPPakPacher, Warning, TEXT("CryptEntryDataImpl - DataSize <= 0: BufferSize=%lld HeaderSize=%lld bEncrypt=%d"),
			InBufferSize, InEntryHeaderSize, (int)bEncrypt);
		return false;
	}

	const int64 AlignedSize = Align(DataSize, FAES::AESBlockSize);
	if (AlignedSize != DataSize)
	{
		// 不对齐说明 caller 漏了 Align()，继续会按 AlignedSize 越界读写 buffer。
		UE_LOG(LogPPakPacher, Error,
			TEXT("CryptEntryDataImpl - DataSize not AES-aligned (would overrun buffer): DataSize=%lld AlignedSize=%lld HeaderSize=%lld bEncrypt=%d"),
			DataSize, AlignedSize, InEntryHeaderSize, (int)bEncrypt);
		return false;
	}
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
// DecryptAndDecompress helpers
// ---------------------------------------------------------------------------

/**
 * 从已解密的 entry buffer 中逐块解压，产出原始字节。
 * @param InDecryptedBuffer  已解密的 [Header | DecryptedData] buffer
 * @param InEntry            对应的 FPakEntry（含 CompressionBlocks/Size/UncompressedSize）
 * @param InPakFile          所属 FPakFile（取 Version + CompressionMethod）
 * @param OutRaw             输出：解压后的原始数据（纯 payload，不含 header）
 * @return true on success
 */
static bool DecompressEntryToRaw(const uint8* InDecryptedBuffer, int64 InBufferSize,
	const FPakEntry& InEntry, const FPakFile& InPakFile, TArray<uint8>& OutRaw)
{
	if (InEntry.CompressionMethodIndex == 0)
	{
		// 未压缩：data 区就是原始数据
		const int64 HeaderSize = InEntry.GetSerializedSize(InPakFile.GetInfo().Version);
		const int64 DataSize = InEntry.UncompressedSize;
		OutRaw.SetNumUninitialized(DataSize);
		FMemory::Memcpy(OutRaw.GetData(), InDecryptedBuffer + HeaderSize, DataSize);
		return true;
	}

	const FName CompressionMethod = InPakFile.GetInfo().GetCompressionMethod(InEntry.CompressionMethodIndex);
	const int64 HeaderSize = InEntry.GetSerializedSize(InPakFile.GetInfo().Version);
	const uint8* DataStart = InDecryptedBuffer + HeaderSize;

	// CompressionBlocks 的 CompressedStart 是相对于 Entry.Offset 的（v5+ relative mode），
	// 即第一个 block 的 CompressedStart == HeaderSize。
	// DataStart 指向的物理位置 = InDecryptedBuffer + HeaderSize，
	// 所以 block[i] 在 DataStart 中的偏移 = CompressedStart[i] - CompressedStart[0]
	const int64 FirstBlockStart = InEntry.CompressionBlocks[0].CompressedStart;

	OutRaw.SetNumUninitialized(InEntry.UncompressedSize);
	int64 DstOffset = 0;

	for (int32 i = 0; i < InEntry.CompressionBlocks.Num(); ++i)
	{
		const int64 CompressedBlockSize = InEntry.CompressionBlocks[i].CompressedEnd - InEntry.CompressionBlocks[i].CompressedStart;
		const int64 BlockDataOffset = InEntry.CompressionBlocks[i].CompressedStart - FirstBlockStart;
		const int64 UncompBlockSize = FMath::Min<int64>(InEntry.UncompressedSize - DstOffset, (int64)InEntry.CompressionBlockSize);

		if (!FCompression::UncompressMemory(CompressionMethod,
			OutRaw.GetData() + DstOffset, (int32)UncompBlockSize,
			DataStart + BlockDataOffset, (int32)CompressedBlockSize))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("DecompressEntryToRaw - UncompressMemory failed. Block=%d CompSize=%lld UncompSize=%lld"),
				i, CompressedBlockSize, UncompBlockSize);
			return false;
		}
		DstOffset += UncompBlockSize;
	}
	return true;
}

/**
 * 将原始字节逐块压缩 + 加密，重建完整的 [Header | EncryptedData] buffer。
 * @param InRawData          原始数据（解压后的 payload）
 * @param InEntry            NewEntry（含压缩参数：CompressionMethodIndex, CompressionBlockSize, Flags 等）
 * @param InPakVersion       New pak 版本号（用于 GetSerializedSize）
 * @param InCompressionMethod  压缩方法 FName
 * @param InEncKey           加密 key（可为 nullptr 如果不加密）
 * @param OutBuffer          输出：完整的 [Header | Data] buffer
 * @return true on success
 */
static bool CompressRawToEntry(const TArray<uint8>& InRawData,
	const FPakEntry& InEntry, int32 InPakVersion, FName InCompressionMethod,
	const FAES::FAESKey* InEncKey, TArray<uint8>& OutBuffer)
{
	if (InEntry.CompressionMethodIndex == 0)
	{
		// 未压缩：直接用 Header + RawData（+ 可能的 AES padding）
		const int64 HeaderSize = InEntry.GetSerializedSize(InPakVersion);
		const int64 DataSizeOnDisk = InEntry.IsEncrypted() ? Align(InRawData.Num(), FAES::AESBlockSize) : InRawData.Num();
		OutBuffer.SetNumZeroed(HeaderSize + DataSizeOnDisk);
		// Header：序列化到临时 buffer 再拷贝
		{
			TArray<uint8> HeaderBuf;
			FMemoryWriter HeaderWriter(HeaderBuf);
			FPakEntry EntryCopy = InEntry;
			EntryCopy.Offset = 0;
			EntryCopy.Serialize(HeaderWriter, InPakVersion);
			check(HeaderBuf.Num() == HeaderSize);
			FMemory::Memcpy(OutBuffer.GetData(), HeaderBuf.GetData(), HeaderSize);
		}
		// Data
		FMemory::Memcpy(OutBuffer.GetData() + HeaderSize, InRawData.GetData(), InRawData.Num());
		// 加密
		if (InEntry.IsEncrypted() && InEncKey)
		{
			FAES::EncryptData(OutBuffer.GetData() + HeaderSize, DataSizeOnDisk, *InEncKey);
		}
		return true;
	}

	// 压缩 entry：逐块压缩
	const int32 NumBlocks = (InRawData.Num() + InEntry.CompressionBlockSize - 1) / InEntry.CompressionBlockSize;
	const int64 HeaderSize = InEntry.GetSerializedSize(InPakVersion);

	// 先压缩所有块到临时 buffer，同时记录每块的压缩大小
	TArray<TArray<uint8>> CompressedBlocks;
	CompressedBlocks.SetNum(NumBlocks);
	int64 TotalCompressedSize = 0;

	for (int32 i = 0; i < NumBlocks; ++i)
	{
		const int64 SrcOffset = (int64)i * InEntry.CompressionBlockSize;
		const int32 BlockUncompSize = (int32)FMath::Min<int64>(InRawData.Num() - SrcOffset, InEntry.CompressionBlockSize);

		int32 MaxCompSize = FCompression::CompressMemoryBound(InCompressionMethod, BlockUncompSize);
		CompressedBlocks[i].SetNumUninitialized(MaxCompSize);

		int32 CompSize = MaxCompSize;
		if (!FCompression::CompressMemory(InCompressionMethod,
			CompressedBlocks[i].GetData(), CompSize,
			InRawData.GetData() + SrcOffset, BlockUncompSize))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("CompressRawToEntry - CompressMemory failed. Block=%d"), i);
			return false;
		}
		CompressedBlocks[i].SetNum(CompSize);

		int64 AlignedBlockSize = InEntry.IsEncrypted() ? Align((int64)CompSize, FAES::AESBlockSize) : (int64)CompSize;
		TotalCompressedSize += AlignedBlockSize;
	}

	// 组装最终 buffer：[Header | Block0(aligned) | Block1(aligned) | ...]
	OutBuffer.SetNumZeroed(HeaderSize + TotalCompressedSize);

	// 填充 Data 区
	int64 WriteOffset = HeaderSize;
	for (int32 i = 0; i < NumBlocks; ++i)
	{
		const int32 CompSize = CompressedBlocks[i].Num();
		FMemory::Memcpy(OutBuffer.GetData() + WriteOffset, CompressedBlocks[i].GetData(), CompSize);
		// AES padding 填充（确定性填充）
		const int64 AlignedSize = InEntry.IsEncrypted() ? Align((int64)CompSize, FAES::AESBlockSize) : (int64)CompSize;
		for (int64 j = CompSize; j < AlignedSize; ++j)
		{
			OutBuffer[WriteOffset + j] = OutBuffer[WriteOffset + (j % CompSize)];
		}
		WriteOffset += AlignedSize;
	}

	// 序列化 Header（需要重建 CompressionBlocks 偏移）
	FPakEntry NewEntry = InEntry;
	NewEntry.Offset = 0;
	NewEntry.Size = TotalCompressedSize;
	NewEntry.UncompressedSize = InRawData.Num();
	NewEntry.CompressionBlocks.SetNum(NumBlocks);

	int64 BlockOffset = HeaderSize; // 相对于 entry 起始
	for (int32 i = 0; i < NumBlocks; ++i)
	{
		NewEntry.CompressionBlocks[i].CompressedStart = BlockOffset;
		NewEntry.CompressionBlocks[i].CompressedEnd = BlockOffset + CompressedBlocks[i].Num();
		int64 AlignedSize = InEntry.IsEncrypted() ? Align((int64)CompressedBlocks[i].Num(), FAES::AESBlockSize) : (int64)CompressedBlocks[i].Num();
		BlockOffset += AlignedSize;
	}

	// 序列化 Header 到临时 buffer 再拷贝到 OutBuffer 头部
	{
		TArray<uint8> HeaderBuf;
		FMemoryWriter HeaderWriter(HeaderBuf);
		NewEntry.Serialize(HeaderWriter, InPakVersion);
		check(HeaderBuf.Num() <= HeaderSize);
		FMemory::Memcpy(OutBuffer.GetData(), HeaderBuf.GetData(), HeaderBuf.Num());
	}

	// 加密整个 Data 区
	if (InEntry.IsEncrypted() && InEncKey)
	{
		FAES::EncryptData(OutBuffer.GetData() + HeaderSize, TotalCompressedSize, *InEncKey);
	}

	return true;
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
	EPPakPatchMode InMode /*= EPPakPatchMode::PakAwareDecryptAndCompress*/, EPakPatchCompressType InCompressType /*= EPakPatchCompressType::None*/,
	FPPakPatcherPerfReport* OutPerfReport /*= nullptr*/)
{
	FPPakFileDataPtr NewPak = LoadOrGetPakData(InNewPakFile);
	FPPakFileDataPtr OldPak = LoadOrGetPakData(InOldPakFile);
	if (!NewPak.IsValid() || !OldPak.IsValid())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CreateDiff - Failed to load pak. New:%s, Old:%s"), *InNewPakFile, *InOldPakFile);
		return false;
	}
	return CreatePakDiff(InPatchFilename, NewPak, OldPak, OutPatch, InMode, InCompressType, OutPerfReport);
}

bool FPPakPatcher::PatchDiff(const FString& InNewPakFilename, const FString& InOldPakFile, const FPResPatchDataPtr& InPatch,
	FPPakPatcherPerfReport* OutPerfReport /*= nullptr*/)
{
	FPPakFileDataPtr OldPak = LoadOrGetPakData(InOldPakFile);
	if (!OldPak.IsValid())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::PatchDiff - Failed to load old pak: %s"), *InOldPakFile);
		return false;
	}
	return PatchPak(InNewPakFilename, OldPak, InPatch, OutPerfReport);
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
	EPPakPatchMode InMode /*= EPPakPatchMode::PakAwareDecryptAndCompress*/, EPakPatchCompressType InCompressType /*= EPakPatchCompressType::None*/,
	FPPakPatcherPerfReport* OutPerfReport /*= nullptr*/)
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
		UE_LOG(LogPPakPacher, Error,
			TEXT("FPPakPatcher::CreatePakDiff - Binary mode should not reach here; it should be dispatched by FPResPatcher::CreateDiff to CreateBinDiff. Filename:%s"),
			*InPatchFilename);
		return false;
	}

	UE_LOG(LogPPakPacher, Display,
		TEXT("FPPakPatcher::CreatePakDiff - Begin. New:%s Old:%s Patch:%s Mode=%s CompressType=%s"),
		*InNewPak->PakFilename, *InOldPak->PakFilename, *InPatchFilename,
		*UEnum::GetValueAsString(InMode),
		*UEnum::GetValueAsString(InCompressType));

	// PakAware entry 预处理策略：直接由 InMode 决定（取代旧 Settings.PakAwarePreprocess 字段）
	const bool bDecrypt    = PPakPatchModeHelper::ShouldDecrypt(InMode);
	const bool bDecompress = PPakPatchModeHelper::ShouldDecompress(InMode);

	// DDC 模式已搁置（详见 TODO_LIST.md「搁置 #26」），入口直接拦截避免产出损坏 pak/ucas。
	if (bDecompress)
	{
		UE_LOG(LogPPakPacher, Error,
			TEXT("FPPakPatcher::CreatePakDiff - PakAwareDecryptAndDecompress mode is currently disabled (see TODO_LIST.md). ")
			TEXT("Use PakAwareNoDecrypt or PakAwareDecryptAndCompress instead. Filename:%s"), *InPatchFilename);
		return false;
	}

	const double StartTime = FPlatformTime::Seconds();

	FPPakPatcherPerfReport PerfReport;
	// 统计 .pak + .utoc + .ucas 总大小
	auto CalcPakGroupSize = [](const FString& PakFilename) -> int64
	{
		int64 Total = FPPakPatcherUtils::GetFileSize(PakFilename);
		const FString BasePath = FPaths::ChangeExtension(PakFilename, TEXT(""));
		const FString UtocPath = BasePath + TEXT(".utoc");
		const FString UcasPath = BasePath + TEXT(".ucas");
		int64 UtocSize = FPPakPatcherUtils::GetFileSize(UtocPath);
		int64 UcasSize = FPPakPatcherUtils::GetFileSize(UcasPath);
		if (UtocSize > 0) Total += UtocSize;
		if (UcasSize > 0) Total += UcasSize;
		return Total;
	};
	PerfReport.TotalOldAssetSize = CalcPakGroupSize(InOldPak->PakFilename);
	PerfReport.TotalNewAssetSize = CalcPakGroupSize(InNewPak->PakFilename);

	OutPatch = MakeShareable(new FPResPatchData());

	// 构建侧：始终同时计算 MD5 + CRC32，运行时按 CheckFileHashType 选择性校验
	const uint32 OldPakCrc32 = FPPakPatcherUtils::CalculateFileCrc32(InOldPak->PakFilename);
	const uint32 NewPakCrc32 = FPPakPatcherUtils::CalculateFileCrc32(InNewPak->PakFilename);

	OutPatch->BeginRecord(InPatchFilename, EPResPatchType::Pak,
		FPaths::GetCleanFilename(InOldPak->PakFilename),
		FPaths::GetCleanFilename(InNewPak->PakFilename),
		InOldPak->GetPakFileMD5(), InNewPak->GetPakFileMD5(),
		OldPakCrc32, NewPakCrc32,
		InCompressType,
		UPPakPatcherSettings::Get().ExternalCompressType,
		(int8)UPPakPatcherSettings::Get().ExternalCompressLevel);

	// 写入 patch 模式到 Header（Patch 阶段必须按相同模式反向重组）
	OutPatch->Header.PatchMode = InMode;

	// Header 元数据：pak 版本、文件大小、主加密 key Guid（解密失败时定位用）
	OutPatch->Header.OldVersion = InOldPak->PakInfo.Version;
	OutPatch->Header.NewVersion = InNewPak->PakInfo.Version;
	OutPatch->Header.OldSize    = FPPakPatcherUtils::GetFileSize(InOldPak->PakFilename);
	OutPatch->Header.NewSize    = FPPakPatcherUtils::GetFileSize(InNewPak->PakFilename);
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

		PerfReport.EntryCountTotal++;

		FString NewHash, OldHash;

		NewPakArchive.Seek(NewEntry.Offset);

		if (UPPakPatcherSettings::Get().bDoubleCheckEntry)
		{
			FPakEntry NewEntryInfo;
			NewEntryInfo.Serialize(NewPakArchive, NewPakFile.GetInfo().Version);
			if (!NewEntryInfo.IndexDataEquals(NewEntry))
			{
				// CreatePakDiff 按 EntryInfos 顺序写 patch；跳过单 entry 会让 ApplyPatch 后续 entry 错位，
				// 因此整 chunk 失败让上层重试。
				UE_LOG(LogPPakPacher, Error,
					TEXT("FPPakPatcher::CreatePakDiff - NewPakEntry double check failed (chunk aborted). filename:%s, PakFilename:%s"),
					*FileName, *InNewPak->PakFilename);
				return false;
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
				FPakEntry OldEntryInfo;
				OldEntryInfo.Serialize(OldPakArchive, OldPakFile.GetInfo().Version);
				if (!OldEntryInfo.IndexDataEquals(OldEntry))
				{
					// 同上：整 chunk 失败而不是跳过单 entry
					UE_LOG(LogPPakPacher, Error,
						TEXT("FPPakPatcher::CreatePakDiff - OldPakEntry double check failed (chunk aborted). filename:%s, PakFilename:%s"),
						*FileName, *InOldPak->PakFilename);
					return false;
				}
				OldHash = BytesToHex(OldEntryInfo.Hash, sizeof(OldEntryInfo.Hash));
			}

			// --- Read ---
			PPATCHER_PERF_BEGIN(&PerfReport, TimeRead);
			FPPakMemoryArchive NewPakWriter(NewRealSize);
			NewPakArchive.Seek(NewEntry.Offset);
			NewPakArchive.Serialize(NewPakWriter.GetData(), NewRealSize);

			FPPakMemoryArchive OldPakWriter(OldRealSize);
			OldPakArchive.Seek(OldEntry.Offset);
			OldPakArchive.Serialize(OldPakWriter.GetData(), OldRealSize);
			PPATCHER_PERF_END(&PerfReport, TimeRead);

			// --- Decrypt ---
			if (bDecrypt && EncKey)
			{
				PPATCHER_PERF_BEGIN(&PerfReport, TimeDecrypt);
				DecryptEntryData(NewPakWriter.GetData(), NewRealSize, NewEntry, NewPakFile, *EncKey);
				DecryptEntryData(OldPakWriter.GetData(), OldRealSize, OldEntry, OldPakFile, *EncKey);
				PPATCHER_PERF_END(&PerfReport, TimeDecrypt);
			}

			// --- Decompress (DecryptAndDecompress only) ---
			if (bDecompress)
			{
				PPATCHER_PERF_BEGIN(&PerfReport, TimeDecompress);
				TArray<uint8> NewRaw, OldRaw;
				if (!DecompressEntryToRaw(NewPakWriter.GetData(), NewRealSize, NewEntry, NewPakFile, NewRaw))
				{
					UE_LOG(LogPPakPacher, Error, TEXT("CreatePakDiff - DecompressEntryToRaw(New) failed. entry:%s"), *FileName);
					return false;
				}
				if (!DecompressEntryToRaw(OldPakWriter.GetData(), OldRealSize, OldEntry, OldPakFile, OldRaw))
				{
					UE_LOG(LogPPakPacher, Error, TEXT("CreatePakDiff - DecompressEntryToRaw(Old) failed. entry:%s"), *FileName);
					return false;
				}
				PPATCHER_PERF_END(&PerfReport, TimeDecompress);

				if (NewRaw.Num() == OldRaw.Num() &&
					FMemory::Memcmp(NewRaw.GetData(), OldRaw.GetData(), NewRaw.Num()) == 0)
				{
					OutPatch->RecordKeep(FileName, NewPakFile, OldPakFile, NewEntry, OldEntry, NewRealSize, OldRealSize);
					PerfReport.EntryCountKeep++;
					PerfReport.KeepAssetPhysicalSize += NewRealSize;
				}
				else
				{
					PPATCHER_PERF_BEGIN(&PerfReport, TimeDiff);
					TArray<uint8> PatchData;
					if (!BinPatcher->CreateDiff(NewRaw.GetData(), NewRaw.Num(), OldRaw.GetData(), OldRaw.Num(), PatchData, InCompressType))
					{
						UE_LOG(LogPPakPacher, Error,
							TEXT("FPPakPatcher::CreatePakDiff - BinPatcher->CreateDiff failed (DDC modify path). entry:%s"), *FileName);
						return false;
					}
					if (!BinPatcher->CheckDiff(NewRaw.GetData(), NewRaw.Num(), OldRaw.GetData(), OldRaw.Num(), PatchData.GetData(), PatchData.Num()))
					{
						// 在 worker 线程内不能 check() abort（会让 TaskRunner 整体崩溃），改为 return false 让上层重试。
						UE_LOG(LogPPakPacher, Error,
							TEXT("FPPakPatcher::CreatePakDiff - HDiff CheckDiff verification failed (DDC modify path, chunk aborted). entry:%s"), *FileName);
						return false;
					}
					PPATCHER_PERF_END(&PerfReport, TimeDiff);
					OutPatch->RecordModify(FileName, NewPakFile, OldPakFile, NewEntry, OldEntry, PatchData, NewRealSize, OldRealSize);
				PerfReport.EntryCountModify++;
				PerfReport.ModifyAssetPhysicalSize += NewRealSize;
				PerfReport.PakEntryDiffSize += PatchData.Num();
			}
		}
		else if (NewHash == OldHash &&
			NewRealSize == OldRealSize &&
			IsEntryPayloadEqual(NewPakWriter.GetData(), OldPakWriter.GetData(), NewEntry, OldEntry, NewPakFile, OldPakFile))
		{
			//record keep same
			OutPatch->RecordKeep(FileName, NewPakFile, OldPakFile, NewEntry, OldEntry, NewRealSize, OldRealSize);
			PerfReport.EntryCountKeep++;
			PerfReport.KeepAssetPhysicalSize += NewRealSize;
		}
		else
		{
			// v6 per-CompressionBlock 路径：DAC + 压缩 entry + New/Old 块数一致 时启用
			const bool bUsePerBlock =
				UPPakPatcherSettings::Get().bUsePerBlockDiff
				&& bDecrypt && !bDecompress
				&& NewEntry.CompressionMethodIndex != 0
				&& NewEntry.CompressionMethodIndex == OldEntry.CompressionMethodIndex
				&& NewEntry.CompressionBlocks.Num() > 0
				&& NewEntry.CompressionBlocks.Num() == OldEntry.CompressionBlocks.Num();

			if (bUsePerBlock)
			{
				PPATCHER_PERF_BEGIN(&PerfReport, TimeDiff);
				const int32 NumBlocks = NewEntry.CompressionBlocks.Num();
				const int64 NewHeaderSize = NewEntry.GetSerializedSize(NewPakFile.GetInfo().Version);
				const int64 OldHeaderSize = OldEntry.GetSerializedSize(OldPakFile.GetInfo().Version);
				int64 NewBufOff = NewHeaderSize;
				int64 OldBufOff = OldHeaderSize;
				int64 TotalPatchSize = 0;
				TArray<TArray<uint8>> BlockPatches;
				BlockPatches.SetNum(NumBlocks);

				for (int32 b = 0; b < NumBlocks; ++b)
				{
					const int64 NewBlockSize = NewEntry.CompressionBlocks[b].CompressedEnd - NewEntry.CompressionBlocks[b].CompressedStart;
					const int64 OldBlockSize = OldEntry.CompressionBlocks[b].CompressedEnd - OldEntry.CompressionBlocks[b].CompressedStart;
					if (NewBufOff + NewBlockSize > NewPakWriter.GetSize() || OldBufOff + OldBlockSize > OldPakWriter.GetSize())
					{
						UE_LOG(LogPPakPacher, Error,
							TEXT("CreatePakDiff(per-block) - block %d range out of buffer. entry:%s"), b, *FileName);
						return false;
					}

					if (!BinPatcher->CreateDiff(
						NewPakWriter.GetData() + NewBufOff, NewBlockSize,
						OldPakWriter.GetData() + OldBufOff, OldBlockSize,
						BlockPatches[b], InCompressType))
					{
						UE_LOG(LogPPakPacher, Error, TEXT("CreatePakDiff(per-block) - CreateDiff failed. entry:%s block:%d"), *FileName, b);
						return false;
					}
					if (!BinPatcher->CheckDiff(
						NewPakWriter.GetData() + NewBufOff, NewBlockSize,
						OldPakWriter.GetData() + OldBufOff, OldBlockSize,
						BlockPatches[b].GetData(), BlockPatches[b].Num()))
					{
						UE_LOG(LogPPakPacher, Error, TEXT("CreatePakDiff(per-block) - CheckDiff failed. entry:%s block:%d"), *FileName, b);
						return false;
					}
					TotalPatchSize += BlockPatches[b].Num();

					NewBufOff += NewEntry.IsEncrypted() ? Align(NewBlockSize, FAES::AESBlockSize) : NewBlockSize;
					OldBufOff += OldEntry.IsEncrypted() ? Align(OldBlockSize, FAES::AESBlockSize) : OldBlockSize;
				}
				PPATCHER_PERF_END(&PerfReport, TimeDiff);

				OutPatch->RecordModifyPerBlock(FileName, NewPakFile, OldPakFile, NewEntry, OldEntry, BlockPatches, NewRealSize, OldRealSize);
				PerfReport.EntryCountModify++;
				PerfReport.EntryCountModifyPerBlock++;
				PerfReport.ModifyAssetPhysicalSize += NewRealSize;
				PerfReport.PakEntryDiffSize += TotalPatchSize;
			}
			else
			{
				//record modified (整 entry 路径)
				PPATCHER_PERF_BEGIN(&PerfReport, TimeDiff);
				TArray<uint8> PatchData;
				if (!BinPatcher->CreateDiff(NewPakWriter.GetData(), NewPakWriter.GetSize(), OldPakWriter.GetData(), OldPakWriter.GetSize(), PatchData, InCompressType))
				{
					UE_LOG(LogPPakPacher, Error,
						TEXT("FPPakPatcher::CreatePakDiff - BinPatcher->CreateDiff failed (entry modify path). entry:%s"), *FileName);
					return false;
				}
				if (!BinPatcher->CheckDiff(NewPakWriter.GetData(), NewPakWriter.GetSize(), OldPakWriter.GetData(), OldPakWriter.GetSize(), PatchData.GetData(), PatchData.Num()))
				{
					// worker 线程内不能 check() abort，改为 return false 让 chunk 重试。
					UE_LOG(LogPPakPacher, Error,
						TEXT("FPPakPatcher::CreatePakDiff - HDiff CheckDiff verification failed (entry modify path, chunk aborted). entry:%s"), *FileName);
					return false;
				}
				PPATCHER_PERF_END(&PerfReport, TimeDiff);
				OutPatch->RecordModify(FileName, NewPakFile, OldPakFile, NewEntry, OldEntry, PatchData, NewRealSize, OldRealSize);
				PerfReport.EntryCountModify++;
				PerfReport.ModifyAssetPhysicalSize += NewRealSize;
				PerfReport.PakEntryDiffSize += PatchData.Num();
			}
		}
		}
		else
		{
			//record new file.
			PPATCHER_PERF_BEGIN(&PerfReport, TimeRead);
			FPPakMemoryArchive MemWriter(NewRealSize);
			NewPakArchive.Seek(NewEntry.Offset);
			NewPakArchive.Serialize(MemWriter.GetData(), NewRealSize);
			PPATCHER_PERF_END(&PerfReport, TimeRead);

			if (bDecrypt && EncKey)
			{
				PPATCHER_PERF_BEGIN(&PerfReport, TimeDecrypt);
				DecryptEntryData(MemWriter.GetData(), NewRealSize, NewEntry, NewPakFile, *EncKey);
				PPATCHER_PERF_END(&PerfReport, TimeDecrypt);
			}

			if (bDecompress)
			{
				PPATCHER_PERF_BEGIN(&PerfReport, TimeDecompress);
				TArray<uint8> NewRaw;
				if (!DecompressEntryToRaw(MemWriter.GetData(), NewRealSize, NewEntry, NewPakFile, NewRaw))
				{
					UE_LOG(LogPPakPacher, Error, TEXT("CreatePakDiff - DecompressEntryToRaw(New-new) failed. entry:%s"), *FileName);
					return false;
				}
				PPATCHER_PERF_END(&PerfReport, TimeDecompress);
				FPPakMemoryArchive RawWriter(NewRaw.Num());
				FMemory::Memcpy(RawWriter.GetData(), NewRaw.GetData(), NewRaw.Num());
				OutPatch->RecordNew(FileName, NewPakFile, OldPakFile, NewEntry, RawWriter, NewRealSize);
				PerfReport.NewEntryFullDataSize += NewRaw.Num();
			}
			else
			{
				OutPatch->RecordNew(FileName, NewPakFile, OldPakFile, NewEntry, MemWriter, NewRealSize);
				PerfReport.NewEntryFullDataSize += NewRealSize;
			}
			PerfReport.EntryCountNew++;
			PerfReport.NewAssetPhysicalSize += NewRealSize;
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

	auto RecordDataBlock = [&](FPPakPatchDataInfo& DataInfo, int64 NewOffset, int64 NewSize, int64 OldOffset, int64 OldSize, bool bIsPatchData) -> bool
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
			if (!BinPatcher->CreateDiff(NewMemWriter.GetData(), NewMemWriter.GetSize(), OldMemWriter.GetData(), OldMemWriter.GetSize(), PatchData, InCompressType))
			{
				UE_LOG(LogPPakPacher, Error,
					TEXT("FPPakPatcher::CreatePakDiff::RecordDataBlock - BinPatcher->CreateDiff failed. NewOffset=%lld OldOffset=%lld"), NewOffset, OldOffset);
				return false;
			}
			if (!BinPatcher->CheckDiff(NewMemWriter.GetData(), NewMemWriter.GetSize(), OldMemWriter.GetData(), OldMemWriter.GetSize(), PatchData.GetData(), PatchData.Num()))
			{
				// worker 线程内不能 check() abort，改为 return false 让 chunk 重试。
				UE_LOG(LogPPakPacher, Error,
					TEXT("FPPakPatcher::CreatePakDiff::RecordDataBlock - HDiff CheckDiff verification failed (chunk aborted). NewOffset=%lld OldOffset=%lld"), NewOffset, OldOffset);
				return false;
			}

			OutPatch->RecordDataBlock(DataInfo, NewOffset, NewSize, OldOffset, OldSize, PatchData.GetData(), PatchData.Num(), bIsPatchData);
		}
		else
		{
			// 旧 pak 没有对应段（OldSize=0），只能按 full data 记录新段。
			OutPatch->RecordDataBlock(DataInfo, NewOffset, NewSize, OldOffset, OldSize, NewMemWriter.GetData(), NewMemWriter.GetSize(), false);
		}
		return true;
	};

	// [1] Primary Index block
	{
		bool bIsPatchData = UPPakPatcherSettings::Get().bBinaryPatchIndexBlock;

		int64 NewOffset = NewPakInfo.IndexOffset;
		int64 NewSize   = NewPakInfo.IndexSize;
		int64 OldOffset = OldPakInfo.IndexOffset;
		int64 OldSize   = OldPakInfo.IndexSize;

		if (!RecordDataBlock(OutPatch->GetPakBody()->IndexPatchInfo, NewOffset, NewSize, OldOffset, OldSize, bIsPatchData))
		{
			return false;
		}
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
			if (!RecordDataBlock(OutPatch->GetPakBody()->PathHashPatchInfo, NewOffset, NewSize, OldOffset, OldSize, bIsPatchData))
			{
				return false;
			}
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
			if (!RecordDataBlock(OutPatch->GetPakBody()->FullDirectoryPatchInfo, NewOffset, NewSize, OldOffset, OldSize, bIsPatchData))
			{
				return false;
			}
		}
	}

	// [4] Head block (FPakInfo footer)
	{
		bool bIsPatchData = UPPakPatcherSettings::Get().bBinaryPatchHeadBlock;

		int64 NewSize = NewPakInfo.GetSerializedSize(NewPakInfo.Version);
		int64 NewOffset = NewPakArchive.TotalSize() - NewSize;
		int64 OldSize = OldPakInfo.GetSerializedSize(OldPakInfo.Version);
		int64 OldOffset = OldPakArchive.TotalSize() - OldSize;

		if (!RecordDataBlock(OutPatch->GetPakBody()->HeadPatchInfo, NewOffset, NewSize, OldOffset, OldSize, bIsPatchData))
		{
			return false;
		}
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
		int64 IoStoreModifyPhysical = 0;
		if (!IoStorePatcher.CreateIoStoreDiff(InNewPak->PakFilename, InOldPak->PakFilename, *OutPatch, &IoStoreModifyPhysical, &PerfReport, InCompressType))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CreatePakDiff - IoStore diff failed for %s"), *InNewPak->PakFilename);
			// 半成品 patch 必须清理：bPrecachePatchDataOnSave=false 时 Writer 已写盘，
			// 不清理会留下"PakBody 完整 + IoStoreBody 缺失"的脏文件。
			OutPatch->AbortRecord();
			OutPatch.Reset();
			return false;
		}
		PerfReport.IoStoreModifyPhysicalSize += IoStoreModifyPhysical;

		// 统计 IoStore diff 大小
		if (const FPIoStorePatchBody* IoBody = OutPatch->GetIoStoreBody())
		{
			PerfReport.IoStoreDiffSize += IoBody->UtocDiffInfo.DataSize;
			if (IoBody->Strategy == EIoStoreDiffStrategy::FileBinary)
			{
				PerfReport.IoStoreDiffSize += IoBody->UcasDiffInfo.DataSize;
			}
			else
			{
				// ChunkAware: 累加每个 chunk 的 DataInfo.DataSize
				for (const FPIoStoreChunkPatchInfo& ChunkInfo : IoBody->ChunkPatchInfos)
				{
					PerfReport.IoStoreDiffSize += ChunkInfo.DataInfo.DataSize;
				}
			}
		}
	}

	OutPatch->EndRecord();

	PerfReport.TimeTotal = FPlatformTime::Seconds() - StartTime;
	PerfReport.LogSummary(TEXT("CreatePakDiff"));

	// 汇总 PerfReport 到 caller 的 Summary（如果传了）
	if (OutPerfReport)
	{
		OutPerfReport->MergeFrom(PerfReport);
	}

	UE_LOG(LogPPakPacher, Display, TEXT("Create pak patch successed. Cost time %.2lfs. Filename:%s"), FPlatformTime::Seconds() - StartTime, *InPatchFilename);

	return true;
}

// ---------------------------------------------------------------------------
// PatchPak
// ---------------------------------------------------------------------------

FArchive* CreatePakPatchWriter(const TCHAR* Filename, const FKeyChain& InKeyChain, bool bSign)
{
	// 用 TUniquePtr 持有底层 FileWriter，确保 new FPSignedPakPatchWriter 抛异常时不泄漏；
	// 成功路径上把所有权 Release() 到 SignedPakPatchWriter（其析构链负责 delete &PakWriter）。
	TUniquePtr<FArchive> RawFileWriter(IFileManager::Get().CreateFileWriter(Filename));
	if (!RawFileWriter)
	{
		return nullptr;
	}

	if (bSign && UPPakPatcherSettings::Get().bUseSignWriter)
	{
		UE_LOG(LogPPakPacher, Display, TEXT("Creating signed pak %s."), Filename);
		// 把 RawFileWriter 所有权转移给 SignedPakPatchWriter（其析构 delete &PakWriter）
		FArchive* RawPtr = RawFileWriter.Release();
		// 注意：如果 new 抛异常（在 UE 禁用异常的默认配置下不会发生，但仍是良好实践），
		// RawPtr 会泄漏；这是 UE 内核常见模式，无法在不接异常的前提下进一步保护。
		return new FPSignedPakPatchWriter(*RawPtr, Filename, InKeyChain.GetSigningKey());
	}

	UE_LOG(LogPPakPacher, Display, TEXT("Creating pak %s."), Filename);
	return RawFileWriter.Release();
}

bool FPPakPatcher::PatchPak(const FString& InNewPakFilename, const FPPakFileDataPtr& InOldPak, const FPResPatchDataPtr& InPatch,
	FPPakPatcherPerfReport* OutPerfReport /*= nullptr*/)
{
	const double StartTime = FPlatformTime::Seconds();
	FPPakPatcherPerfReport PerfReport;

	FString NewPakFilename = InNewPakFilename;
	FPaths::MakeStandardFilename(NewPakFilename);
	if (NewPakFilename.IsEmpty())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::PatchPak - NewPakFilename was empty."));
		return false;
	}
	if (!InOldPak.IsValid())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::PatchPak - InOldPak was Invalid. NewPakFilename:%s"), *NewPakFilename);
		return false;
	}

	if (!InPatch.IsValid())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::PatchPak - InPatch was Invalid. NewPakFilename:%s"), *NewPakFilename);
		return false;
	}

	UE_LOG(LogPPakPacher, Display,
		TEXT("FPPakPatcher::PatchPak - Begin. NewPak:%s OldPak:%s PatchMode=%s HasIoStoreBody=%d"),
		*NewPakFilename, *InOldPak->PakFilename,
		*UEnum::GetValueAsString(InPatch->Header.PatchMode),
		InPatch->GetIoStoreBody() ? 1 : 0);

	// 运行时：patch 里记录的 mode（与构建侧一致）
	const EPPakPatchMode PatchMode = InPatch->Header.PatchMode;
	const bool bDecrypt    = PPakPatchModeHelper::ShouldDecrypt(PatchMode);
	const bool bDecompress = PPakPatchModeHelper::ShouldDecompress(PatchMode);
	const FAES::FAESKey* EncKey = nullptr;

	// DDC 模式已搁置（详见 TODO_LIST.md「搁置 #26」），运行时拒绝避免产出损坏 pak。
	if (bDecompress)
	{
		UE_LOG(LogPPakPacher, Error,
			TEXT("FPPakPatcher::PatchPak - DecryptAndDecompress mode is currently disabled (see TODO_LIST.md). ")
			TEXT("Patch file declares DDC strategy; please regenerate with NoDecrypt or DecryptAndCompress. NewPak:%s"),
			*NewPakFilename);
		return false;
	}

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

		if (bDecompress)
		{
			// DecryptAndDecompress: 重压缩后 entry 大小不确定，跳过 offset 对齐
			// 顺序写，不做 padding
		}
		else
		{
			check(FilePatchInfo.DataInfo.NewOffset >= Writer->Tell());
			SerializePaddingToOffset(FilePatchInfo.DataInfo.NewOffset);
		}
		FString Filename = FilePatchInfo.FileName;
		PerfReport.EntryCountTotal++;

		if (FilePatchInfo.PatchType == EPakFilePatchType::Keep || FilePatchInfo.PatchType == EPakFilePatchType::Modify)
		{
			FPakEntry OldEntry;
			FString OldHash;
			FPakFile::EFindResult FoundResult = OldPakFile.Find(OldPakFile.GetMountPoint() / FilePatchInfo.FileName, &OldEntry);
			if (FoundResult == FPakFile::EFindResult::Found)
			{
				OldPakArchive.Seek(OldEntry.Offset);
				if (UPPakPatcherSettings::Get().bDoubleCheckEntry)
				{
					FPakEntry OldEntryInfo;
					OldEntryInfo.Serialize(OldPakArchive, OldPakFile.GetInfo().Version);
					if (!OldEntryInfo.IndexDataEquals(OldEntry))
					{
						// PatchPak 也按顺序写 NewPak；跳过单 entry 会错位，整 chunk 失败让上层重试。
						UE_LOG(LogPPakPacher, Error,
							TEXT("FPPakPatcher::PatchPak - OldPakEntry double check failed (chunk aborted). filename:%s, OldPakFilename:%s"),
							*Filename, *InOldPak->PakFilename);
						return false;
					}
					OldHash = BytesToHex(OldEntryInfo.Hash, sizeof(OldEntryInfo.Hash));
				}
			}
			else
			{
				UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::PatchPak - Can't find pak file:[%s], in pak:[%s]"), *Filename, *InOldPak->PakFilename);
				return false;
			}

			// --- Read Old ---
			PPATCHER_PERF_BEGIN(&PerfReport, TimeRead);
			int64 OldRealSize = FilePatchInfo.OldFileRealSize;
			// patch 内 OldOffset 必须等于运行时 OldPakFile.Find() 的 entry offset；
			// 不一致说明 patch 错配 / old pak 被改写，继续读会产出 corrupted new pak。
			if (OldEntry.Offset != FilePatchInfo.DataInfo.OldOffset)
			{
				UE_LOG(LogPPakPacher, Error,
					TEXT("FPPakPatcher::PatchPak - Old entry offset mismatch (chunk aborted). filename:%s OldEntry.Offset=%lld PatchOldOffset=%lld OldPakFilename:%s"),
					*Filename, OldEntry.Offset, FilePatchInfo.DataInfo.OldOffset, *InOldPak->PakFilename);
				return false;
			}
			FPPakMemoryArchive OldPakMemory(OldRealSize);
			OldPakArchive.Seek(FilePatchInfo.DataInfo.OldOffset);
			OldPakArchive.Serialize(OldPakMemory.GetData(), OldRealSize);
			PPATCHER_PERF_END(&PerfReport, TimeRead);

			if (FilePatchInfo.PatchType == EPakFilePatchType::Keep)
			{
				PPATCHER_PERF_BEGIN(&PerfReport, TimeWrite);
				Write(OldPakMemory.GetData(), OldPakMemory.GetSize());
				PPATCHER_PERF_END(&PerfReport, TimeWrite);
				PerfReport.EntryCountKeep++;
				continue;
			}
			else if (FilePatchInfo.PatchType == EPakFilePatchType::Modify)
			{
				PerfReport.EntryCountModify++;

				if (bDecompress)
				{
					PPATCHER_PERF_BEGIN(&PerfReport, TimeDecrypt);
					if (EncKey) DecryptEntryData(OldPakMemory.GetData(), OldRealSize, OldEntry, OldPakFile, *EncKey);
					PPATCHER_PERF_END(&PerfReport, TimeDecrypt);

					PPATCHER_PERF_BEGIN(&PerfReport, TimeDecompress);
					TArray<uint8> OldRaw;
					DecompressEntryToRaw(OldPakMemory.GetData(), OldRealSize, OldEntry, OldPakFile, OldRaw);
					PPATCHER_PERF_END(&PerfReport, TimeDecompress);

					PPATCHER_PERF_BEGIN(&PerfReport, TimePatch);
					TArray<uint8> PatchData;
					InPatch->GetFilePatchData(FilePatchInfo.DataInfo, PatchData);
					TArray<uint8> NewRaw;
					NewRaw.SetNumUninitialized(FilePatchInfo.Entry.UncompressedSize);
					BinPatcher->Patch(NewRaw.GetData(), NewRaw.Num(), OldRaw.GetData(), OldRaw.Num(), PatchData.GetData(), PatchData.Num());
					PPATCHER_PERF_END(&PerfReport, TimePatch);

					PPATCHER_PERF_BEGIN(&PerfReport, TimeCompress);
					FName CompMethod = OldPakFile.GetInfo().GetCompressionMethod(FilePatchInfo.Entry.CompressionMethodIndex);
					TArray<uint8> NewBuffer;
					CompressRawToEntry(NewRaw, FilePatchInfo.Entry, InPatch->Header.NewVersion, CompMethod, EncKey, NewBuffer);
					PPATCHER_PERF_END(&PerfReport, TimeCompress);
					// Note: CompressRawToEntry includes encryption internally

					PPATCHER_PERF_BEGIN(&PerfReport, TimeWrite);
					Write(NewBuffer.GetData(), NewBuffer.Num());
					PPATCHER_PERF_END(&PerfReport, TimeWrite);
				}
				else
				{
					// DecryptAndCompress / NoDecrypt
					if (bDecrypt && EncKey)
					{
						PPATCHER_PERF_BEGIN(&PerfReport, TimeDecrypt);
						DecryptEntryData(OldPakMemory.GetData(), OldRealSize, OldEntry, OldPakFile, *EncKey);
						PPATCHER_PERF_END(&PerfReport, TimeDecrypt);
					}

					// v6 per-CompressionBlock 路径：BlockPatches 非空表示按 block 单独 patch
					const bool bPerBlock = !FilePatchInfo.BlockPatches.IsEmpty();
					if (bPerBlock)
					{
						PerfReport.EntryCountModifyPerBlock++;	// Apply 端 perf 计数（与 Create 端对称）
					}

					FPPakMemoryArchive NewPakMemory(FilePatchInfo.FileRealSize);

					if (bPerBlock)
					{
						const int32 NumBlocks = FilePatchInfo.Entry.CompressionBlocks.Num();
						const int64 NewHeaderSize = FilePatchInfo.Entry.GetSerializedSize(InPatch->Header.NewVersion);
						const int64 OldHeaderSize = OldEntry.GetSerializedSize(OldPakFile.GetInfo().Version);

						// 1) 重建 NewEntry header
						{
							TArray<uint8> HeaderBuf;
							FMemoryWriter Hw(HeaderBuf);
							FPakEntry HeaderCopy = FilePatchInfo.Entry;
							HeaderCopy.Serialize(Hw, InPatch->Header.NewVersion);
							if (HeaderBuf.Num() != NewHeaderSize)
							{
								UE_LOG(LogPPakPacher, Error,
									TEXT("PatchPak(per-block) - header size mismatch (got=%d expect=%lld). filename:%s"),
									HeaderBuf.Num(), NewHeaderSize, *Filename);
								return false;
							}
							FMemory::Memcpy(NewPakMemory.GetData(), HeaderBuf.GetData(), NewHeaderSize);
						}

						// 2) 按 block patch
						PPATCHER_PERF_BEGIN(&PerfReport, TimePatch);
						int64 NewBufOff = NewHeaderSize;
						int64 OldBufOff = OldHeaderSize;
						for (int32 b = 0; b < NumBlocks; ++b)
						{
							const int64 NewBlockSize = FilePatchInfo.Entry.CompressionBlocks[b].CompressedEnd - FilePatchInfo.Entry.CompressionBlocks[b].CompressedStart;
							const int64 OldBlockSize = OldEntry.CompressionBlocks[b].CompressedEnd - OldEntry.CompressionBlocks[b].CompressedStart;

							if (NewBufOff + NewBlockSize > NewPakMemory.GetSize() || OldBufOff + OldBlockSize > OldPakMemory.GetSize())
							{
								UE_LOG(LogPPakPacher, Error,
									TEXT("PatchPak(per-block) - block %d range out of buffer. filename:%s"), b, *Filename);
								return false;
							}

							TArray<uint8> BlockPatchData;
							InPatch->GetFilePatchData(FilePatchInfo.BlockPatches[b].BlockPatchData, BlockPatchData);
							BinPatcher->Patch(NewPakMemory.GetData() + NewBufOff, NewBlockSize,
								OldPakMemory.GetData() + OldBufOff, OldBlockSize,
								BlockPatchData.GetData(), BlockPatchData.Num());

							NewBufOff += FilePatchInfo.Entry.IsEncrypted() ? Align(NewBlockSize, FAES::AESBlockSize) : NewBlockSize;
							OldBufOff += OldEntry.IsEncrypted() ? Align(OldBlockSize, FAES::AESBlockSize) : OldBlockSize;
						}
						PPATCHER_PERF_END(&PerfReport, TimePatch);
					}
					else
					{
						PPATCHER_PERF_BEGIN(&PerfReport, TimePatch);
						TArray<uint8> PatchData;
						InPatch->GetFilePatchData(FilePatchInfo.DataInfo, PatchData);
						BinPatcher->Patch(NewPakMemory.GetData(), NewPakMemory.GetSize(),
							OldPakMemory.GetData(), OldPakMemory.GetSize(),
							PatchData.GetData(), PatchData.Num());
						PPATCHER_PERF_END(&PerfReport, TimePatch);
					}

					if (bDecrypt && EncKey)
					{
						PPATCHER_PERF_BEGIN(&PerfReport, TimeEncrypt);
						EncryptEntryData(NewPakMemory.GetData(), NewPakMemory.GetSize(),
							FilePatchInfo.Entry, InPatch->Header.NewVersion, *EncKey);
						PPATCHER_PERF_END(&PerfReport, TimeEncrypt);
					}

					PPATCHER_PERF_BEGIN(&PerfReport, TimeWrite);
					Write(NewPakMemory.GetData(), NewPakMemory.GetSize());
					PPATCHER_PERF_END(&PerfReport, TimeWrite);
				}
				continue;
			}
		}
		else if (FilePatchInfo.PatchType == EPakFilePatchType::New)
		{
			PerfReport.EntryCountNew++;
			TArray<uint8> PatchData;
			InPatch->GetFilePatchData(FilePatchInfo.DataInfo, PatchData);

			if (bDecompress)
			{
				TArray<uint8> NewRaw;
				NewRaw.SetNumUninitialized(FilePatchInfo.DataInfo.DataSize);
				FMemory::Memcpy(NewRaw.GetData(), PatchData.GetData(), FilePatchInfo.DataInfo.DataSize);

				FName CompMethod = OldPakFile.GetInfo().GetCompressionMethod(FilePatchInfo.Entry.CompressionMethodIndex);
				TArray<uint8> NewBuffer;
				PPATCHER_PERF_BEGIN(&PerfReport, TimeCompress);
				CompressRawToEntry(NewRaw, FilePatchInfo.Entry, InPatch->Header.NewVersion, CompMethod, EncKey, NewBuffer);
				PPATCHER_PERF_END(&PerfReport, TimeCompress);

				PPATCHER_PERF_BEGIN(&PerfReport, TimeWrite);
				Write(NewBuffer.GetData(), NewBuffer.Num());
				PPATCHER_PERF_END(&PerfReport, TimeWrite);
			}
			else if (bDecrypt && EncKey && FilePatchInfo.Entry.IsEncrypted())
			{
				FPPakMemoryArchive NewEntryMem(FilePatchInfo.DataInfo.DataSize);
				FMemory::Memcpy(NewEntryMem.GetData(), PatchData.GetData(), FilePatchInfo.DataInfo.DataSize);

				PPATCHER_PERF_BEGIN(&PerfReport, TimeEncrypt);
				EncryptEntryData(NewEntryMem.GetData(), NewEntryMem.GetSize(),
					FilePatchInfo.Entry, InPatch->Header.NewVersion, *EncKey);
				PPATCHER_PERF_END(&PerfReport, TimeEncrypt);

				PPATCHER_PERF_BEGIN(&PerfReport, TimeWrite);
				Write(NewEntryMem.GetData(), NewEntryMem.GetSize());
				PPATCHER_PERF_END(&PerfReport, TimeWrite);
			}
			else
			{
				PPATCHER_PERF_BEGIN(&PerfReport, TimeWrite);
				Write(PatchData.GetData(), PatchData.Num());
				PPATCHER_PERF_END(&PerfReport, TimeWrite);
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
		if (!bDecompress)
		{
			SerializePaddingToOffset(Info.NewOffset);
		}
		TArray<uint8> PatchData;
		InPatch->GetFilePatchData(Info, PatchData);
		if (Info.bIsPatchData)
		{
			FPPakMemoryArchive OldPakMemory(Info.OldSize);
			OldPakArchive.Seek(Info.OldOffset);
			OldPakArchive.Serialize(OldPakMemory.GetData(), Info.OldSize);

			FPPakMemoryArchive NewPakMemory(Info.NewSize);

			BinPatcher->Patch(NewPakMemory.GetData(), NewPakMemory.GetSize(),
				OldPakMemory.GetData(), OldPakMemory.GetSize(),
				PatchData.GetData(), PatchData.Num());

			Writer->Serialize(NewPakMemory.GetData(), NewPakMemory.GetSize());
		}
		else
		{
			Writer->Serialize(PatchData.GetData(), PatchData.Num());
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
			TArray<uint8> PatchData;
			InPatch->GetFilePatchData(Info, PatchData);
			FString SignFilename = FPaths::ChangeExtension(NewPakFilename, UPPakPatcherSettings::Get().NewSignExtension);
			FArchive* SignFileWriter = IFileManager::Get().CreateFileWriter(*SignFilename);
			if (SignFileWriter)
			{
				SignFileWriter->Serialize(PatchData.GetData(), PatchData.Num());
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
		if (!IoStorePatcher.PatchIoStore(InNewPakFilename, InOldPak->PakFilename, *InPatch, &PerfReport))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::PatchPak - IoStore patch failed for %s"), *InNewPakFilename);
			return false;
		}
	}

	PerfReport.TimeTotal = FPlatformTime::Seconds() - StartTime;
	PerfReport.LogSummary(TEXT("PatchPak"));

	// 汇总 PerfReport 到 caller 的 Summary（如果传了）
	if (OutPerfReport)
	{
		OutPerfReport->MergeFrom(PerfReport);
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

	// Padding 校验（chunk 间填充是否纯零）：dev-only invariant，生产保持 false。
	// UnrealPak 通常 zero-pad，但部分 cook 配置可能写非零字节误报，故默认关闭。
	const int64 PaddingBufferSize = 64 * 1024;
	FPPakPatcherMemory PaddingBuffer;
	const bool bCheckReaderPadding = false;
	auto CheckReaderPaddingToOffset = [&](FArchive& Reader, int64 InToOffset) -> bool
		{
			if (!bCheckReaderPadding)
			{
				return true;
			}
			int64 Size = InToOffset - Reader.Tell();
			if (Size == 0)
			{
				return true;
			}
			if (Reader.Tell() + Size > Reader.TotalSize())
			{
				return false;
			}
			if (PaddingBuffer.GetSize() == 0)
			{
				PaddingBuffer.Resize(PaddingBufferSize);
				FMemory::Memset(PaddingBuffer.GetData(), 0, PaddingBufferSize);
			}
			// 大 padding 分块比较（每次最多 PaddingBufferSize 字节）
			int64 Remaining = Size;
			while (Remaining > 0)
			{
				const int64 ChunkSize = FMath::Min<int64>(Remaining, PaddingBufferSize);
				FPPakPatcherMemory Memory;
				Memory.Resize(ChunkSize);
				Reader.Serialize(Memory.GetData(), ChunkSize);
				if (FMemory::Memcmp(Memory.GetData(), PaddingBuffer.GetData(), ChunkSize) != 0)
				{
					return false;
				}
				Remaining -= ChunkSize;
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

				TArray<uint8> PatchData;
				InPatch->GetFilePatchData(FilePatchInfo.DataInfo, PatchData);

				BinPatcher->Patch(NewPakMemory2.GetData(), NewPakMemory2.GetSize(),
					OldPakMemory.GetData(), OldPakMemory.GetSize(),
					PatchData.GetData(), PatchData.Num());

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
			uint8* PatchData = nullptr;
			TArray<uint8> PatchDataBuf;
			InPatch->GetFilePatchData(FilePatchInfo.DataInfo, PatchDataBuf);
			PatchData = PatchDataBuf.GetData();
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
		TArray<uint8> PatchData;
		InPatch->GetFilePatchData(Info, PatchData);
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
				PatchData.GetData(), PatchData.Num());

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
			if (FMemory::Memcmp(PatchData.GetData(), NewPakMemory.GetData(), Info.NewSize) != 0)
			{
				UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - [%s] block memory compare failed."), BlockName);
				return false;
			}

			// Hash 校验（full data 情况）
			if (ExpectedHash != nullptr)
			{
				TArray<uint8> HashCopy;
				HashCopy.Append(PatchData.GetData(), Info.NewSize);
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
