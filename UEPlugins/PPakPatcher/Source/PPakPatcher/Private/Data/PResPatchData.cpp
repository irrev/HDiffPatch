#include "Data/PResPatchData.h"
#include "Data/PPakPatcherDataType.h"
#include "PPakPatcherSettings.h"
#include "Compression/OodleDataCompression.h"

//#define PATCH_ENSURE
#define PATCH_ENSURE ensure

namespace
{
	/** 映射 EPPatchExternalCompressType → FOodleDataCompression::ECompressor。返回是否需要外部压缩。 */
	bool ResolveOodleCompressor(EPPatchExternalCompressType InType, FOodleDataCompression::ECompressor& OutCompressor)
	{
		switch (InType)
		{
		case EPPatchExternalCompressType::Oodle_Selkie:    OutCompressor = FOodleDataCompression::ECompressor::Selkie;    return true;
		case EPPatchExternalCompressType::Oodle_Mermaid:   OutCompressor = FOodleDataCompression::ECompressor::Mermaid;   return true;
		case EPPatchExternalCompressType::Oodle_Kraken:    OutCompressor = FOodleDataCompression::ECompressor::Kraken;    return true;
		case EPPatchExternalCompressType::Oodle_Leviathan: OutCompressor = FOodleDataCompression::ECompressor::Leviathan; return true;
		case EPPatchExternalCompressType::None:
		default:                                           return false;
		}
	}
}

// -----------------------------------------------------------------------------
// FPResPatchHead
// -----------------------------------------------------------------------------

void FPResPatchHead::Serialize(FArchive& Ar)
{
	Ar << DataBlockOffset;
	Ar << DataBlockSize;
	Ar << IndexOffset;
	Ar << IndexSize;
	// v8 起 DataBlock 由各 RecordData 独立压缩拼接而成，无需 Head 级别块表
}

bool FPResPatchHead::IsEqual(FPResPatchHead& Other)
{
	bool bIsEqual = true;
	bIsEqual &= PATCH_ENSURE(this->DataBlockOffset == Other.DataBlockOffset);
	bIsEqual &= PATCH_ENSURE(this->DataBlockSize == Other.DataBlockSize);
	bIsEqual &= PATCH_ENSURE(this->IndexOffset == Other.IndexOffset);
	bIsEqual &= PATCH_ENSURE(this->IndexSize == Other.IndexSize);
	return bIsEqual;
}

// -----------------------------------------------------------------------------
// FPResPatchHeader
// -----------------------------------------------------------------------------

void FPResPatchHeader::Serialize(FArchive& Ar)
{
	uint8 TypeByte = static_cast<uint8>(Type);
	Ar << TypeByte;
	if (Ar.IsLoading())
	{
		Type = static_cast<EPResPatchType>(TypeByte);
	}

	Ar << FormatVersion;
	Ar << OldFileName;
	Ar << NewFileName;
	Ar << OldMD5;
	Ar << NewMD5;
	Ar << OldCrc32;
	Ar << NewCrc32;
	Ar << OldVersion;
	Ar << NewVersion;
	Ar << OldSize;
	Ar << NewSize;

	uint8 CompressByte = static_cast<uint8>(CompressType);
	Ar << CompressByte;
	if (Ar.IsLoading())
	{
		CompressType = static_cast<EPakPatchCompressType>(CompressByte);
	}

	uint8 ExternalCompressByte = static_cast<uint8>(ExternalCompressType);
	Ar << ExternalCompressByte;
	if (Ar.IsLoading())
	{
		ExternalCompressType = static_cast<EPPatchExternalCompressType>(ExternalCompressByte);
	}
	Ar << ExternalCompressLevel;

	// v7：合并原 PakAwarePreprocess + GenerateMode 为单一 PatchMode（uint8）
	uint8 PatchModeByte = static_cast<uint8>(PatchMode);
	Ar << PatchModeByte;
	if (Ar.IsLoading())
	{
		PatchMode = static_cast<EPPakPatchMode>(PatchModeByte);
	}

	Ar << PrincipalEncryptionKeyGuid;
}

bool FPResPatchHeader::IsEqual(FPResPatchHeader& Other)
{
	bool bIsEqual = true;
	bIsEqual &= PATCH_ENSURE(Type == Other.Type);
	bIsEqual &= PATCH_ENSURE(FormatVersion == Other.FormatVersion);
	bIsEqual &= PATCH_ENSURE(OldFileName == Other.OldFileName);
	bIsEqual &= PATCH_ENSURE(NewFileName == Other.NewFileName);
	bIsEqual &= PATCH_ENSURE(OldMD5 == Other.OldMD5);
	bIsEqual &= PATCH_ENSURE(NewMD5 == Other.NewMD5);
	bIsEqual &= PATCH_ENSURE(OldCrc32 == Other.OldCrc32);
	bIsEqual &= PATCH_ENSURE(NewCrc32 == Other.NewCrc32);
	bIsEqual &= PATCH_ENSURE(OldVersion == Other.OldVersion);
	bIsEqual &= PATCH_ENSURE(NewVersion == Other.NewVersion);
	bIsEqual &= PATCH_ENSURE(OldSize == Other.OldSize);
	bIsEqual &= PATCH_ENSURE(NewSize == Other.NewSize);
	bIsEqual &= PATCH_ENSURE(CompressType == Other.CompressType);
	bIsEqual &= PATCH_ENSURE(ExternalCompressType == Other.ExternalCompressType);
	bIsEqual &= PATCH_ENSURE(ExternalCompressLevel == Other.ExternalCompressLevel);
	bIsEqual &= PATCH_ENSURE(PatchMode == Other.PatchMode);
	bIsEqual &= PATCH_ENSURE(PrincipalEncryptionKeyGuid == Other.PrincipalEncryptionKeyGuid);
	return bIsEqual;
}

// -----------------------------------------------------------------------------
// FPResPatchData
// -----------------------------------------------------------------------------

FPResPatchData::FPResPatchData()
{
}

FPResPatchData::~FPResPatchData()
{
	if (Reader)
	{
		Reader->Close();
		delete Reader;
	}
	if (Writer)
	{
		Writer->Close();
		delete Writer;
	}
}

bool FPResPatchData::IsEqual(FPResPatchData& Other)
{
	bool bIsEqual = true;
	bIsEqual &= PATCH_ENSURE(Header.IsEqual(Other.Header));
	bIsEqual &= PATCH_ENSURE(Head.IsEqual(Other.Head));
	bIsEqual &= PATCH_ENSURE(HeadOffset == Other.HeadOffset);

	// Body 按 Type 对比
	switch (Header.Type)
	{
	case EPResPatchType::Bin:
		if (BinBody.IsValid() && Other.BinBody.IsValid())
			bIsEqual &= BinBody->IsEqual(*Other.BinBody);
		break;
	case EPResPatchType::Pak:
		if (PakBody.IsValid() && Other.PakBody.IsValid())
			bIsEqual &= PakBody->IsEqual(*Other.PakBody);
		// Pak 类型可携带 IoStoreBody 同伴（UE5 cook 产物：.pak + .utoc + .ucas）
		// 两侧 IoStoreBody 必须同时有/无；任一不一致视为不等。
		if (IoStoreBody.IsValid() != Other.IoStoreBody.IsValid())
		{
			bIsEqual &= PATCH_ENSURE(false);
		}
		else if (IoStoreBody.IsValid() && Other.IoStoreBody.IsValid())
		{
			bIsEqual &= IoStoreBody->IsEqual(*Other.IoStoreBody);
		}
		break;
	case EPResPatchType::IoStore:
		if (IoStoreBody.IsValid() && Other.IoStoreBody.IsValid())
			bIsEqual &= IoStoreBody->IsEqual(*Other.IoStoreBody);
		break;
	default:
		break;
	}

	if (this->bUsePrecache && Other.bUsePrecache)
	{
		bIsEqual &= FMemory::Memcmp(this->Data.GetData(), Other.Data.GetData(), this->Data.GetSize()) == 0;
	}
	return bIsEqual;
}

// -----------------------------------------------------------------------------
// Save / Load
// -----------------------------------------------------------------------------

bool FPResPatchData::SaveToFile(const FString& InPatchFilename)
{
	if (!bUsePrecache)
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatchData::SaveToFile - only supported when use precache memory. file: %s"), *PatchFilename);
		return false;
	}
	PatchFilename = InPatchFilename;
	if (PatchFilename.IsEmpty())
	{
		PatchFilename = FPaths::ChangeExtension(Header.OldFileName, TEXT(".patch"));
	}
	FPaths::MakeStandardFilename(PatchFilename);

	if (!SetupWriter(PatchFilename))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatchData::SaveToFile - create writer failed. %s"), *PatchFilename);
		return false;
	}

	// step 1: write data block
	// v8: Data 池里的字节已经是 per-entry 压缩态（RecordData 时即压缩），SaveToFile 直接整段落盘。
	{
		Head.DataBlockOffset = Writer->Tell();
		Head.DataBlockSize   = Data.GetSize();
		if (Data.GetSize() > 0)
		{
			Writer->Serialize(Data.GetData(), Data.GetSize());
		}

		if (Header.ExternalCompressType != EPPatchExternalCompressType::None)
		{
			UE_LOG(LogPPakPacher, Display,
				TEXT("FPResPatchData::SaveToFile - per-entry external compress: %s level=%d DataBlockSize=%lld File:%s"),
				*UEnum::GetValueAsString(Header.ExternalCompressType),
				(int32)Header.ExternalCompressLevel,
				Head.DataBlockSize, *PatchFilename);
		}
	}

	FinalizeWriteFile();
	return true;
}

bool FPResPatchData::LoadFromFile(const FString& InPatchFilename)
{
	PatchFilename = InPatchFilename;
	FPaths::MakeStandardFilename(PatchFilename);

	// 重置实例状态：允许同一 FPResPatchData 实例反复 LoadFromFile 不残留旧 patch 数据。
	// （SetupReader 内部会释放旧 Reader；这里清的是 v8 per-entry 解压临时 buffer。）
	ScratchCompressBuf.Reset();
	Data.Resize(0);
	Head = FPResPatchHead();
	Header = FPResPatchHeader();
	HeadOffset = 0;

	if (!SetupReader(PatchFilename))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatchData::LoadFromFile - create reader failed. %s"), *PatchFilename);
		return false;
	}

	DataSourceType = EPPatchDataSourceType::Load;
	bUsePrecache = UPPakPatcherSettings::Get().bPrecachePatchDataOnLoad;

	// step 1: HeadOffset
	Reader->Seek(Reader->TotalSize() - sizeof(HeadOffset));
	(*Reader) << HeadOffset;

	// step 2: Head
	Reader->Seek(HeadOffset);
	Head.Serialize(*Reader);

	// step 3: Header + Body
	Reader->Seek(Head.IndexOffset);
	Header.Serialize(*Reader);

	// 版本守卫：序列化布局会随 FormatVersion 变化（详见 PResPatchData.h 中的 changelog）；
	// 旧版 patch 用新 reader 解析会错位读取，必须直接拒绝并提示用户重新生成。
	if (Header.FormatVersion != PRES_PATCH_FORMAT_VERSION)
	{
		UE_LOG(LogPPakPacher, Error,
			TEXT("FPResPatchData::LoadFromFile - Format version mismatch. File:%s ExpectVersion:%d ActualVersion:%d. ")
			TEXT("Patch file is incompatible; please regenerate with the current build pipeline."),
			*PatchFilename, PRES_PATCH_FORMAT_VERSION, Header.FormatVersion);
		Reader->Close();
		delete Reader;
		Reader = nullptr;
		return false;
	}

	EnsureBodyAllocated();
	SerializeBody(*Reader);

	// step 4: DataBlock
	// v8: per-entry 压缩 — DataBlock 是 RecordData 时即压缩好的字节流；不再有整块切片压缩。
	//   - bPrecachePatchDataOnLoad=true：把整个 DataBlock 整段拷到 Data 池（GetFilePatchData 时按需解压每条）
	//   - 否则保留 Reader，GetFilePatchData 直接磁盘按 (offset, CompressedSize) seek+read+解压
	if (bUsePrecache)
	{
		Reader->Seek(Head.DataBlockOffset);
		Data.Resize(Head.DataBlockSize);
		if (Head.DataBlockSize > 0)
		{
			Reader->Serialize(Data.GetData(), Data.GetSize());
		}

		Reader->Close();
		delete Reader;
		Reader = nullptr;
	}
	// !bUsePrecache：保留 Reader，GetFilePatchData 按需流式读取并按 entry 解压

	return true;
}

// -----------------------------------------------------------------------------
// Begin / End Record
// -----------------------------------------------------------------------------

bool FPResPatchData::BeginRecord(const FString& InPatchFilename, EPResPatchType InType,
	const FString& InOldFileName, const FString& InNewFileName,
	const FString& InOldMD5, const FString& InNewMD5,
	uint32 InOldCrc32, uint32 InNewCrc32,
	EPakPatchCompressType InCompressType,
	EPPatchExternalCompressType InExternalCompressType /*= EPPatchExternalCompressType::None*/,
	int8 InExternalCompressLevel /*= 0*/)
{
	Header = FPResPatchHeader();
	Header.Type = InType;
	Header.FormatVersion = PRES_PATCH_FORMAT_VERSION;
	Header.OldFileName = InOldFileName;
	Header.NewFileName = InNewFileName;
	Header.OldMD5 = InOldMD5;
	Header.NewMD5 = InNewMD5;
	Header.OldCrc32 = InOldCrc32;
	Header.NewCrc32 = InNewCrc32;
	Header.CompressType = InCompressType;
	Header.ExternalCompressType = InExternalCompressType;
	Header.ExternalCompressLevel = InExternalCompressLevel;

	// 按 Type 分配 Body
	PakBody.Reset();
	BinBody.Reset();
	IoStoreBody.Reset();
	EnsureBodyAllocated();

	DataSourceType = EPPatchDataSourceType::Record;
	bUsePrecache = UPPakPatcherSettings::Get().bPrecachePatchDataOnSave;

	// v8: per-entry 压缩 — 不再强制 precache（每条记录独立压缩，流式模式同样工作正常）。

	// 提前记录 PatchFilename（precache 模式下 caller 显式调 SaveToFile 落盘）
	PatchFilename = InPatchFilename;
	FPaths::MakeStandardFilename(PatchFilename);

	if (!bUsePrecache)
	{
		if (!SetupWriter(PatchFilename))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPResPatchData::BeginRecord - create writer failed."));
			return false;
		}
		Head.DataBlockOffset = Writer->Tell();
	}

	return true;
}

bool FPResPatchData::EndRecord()
{
	if (bUsePrecache)
	{
		// precache 模式：原始未压缩大小已在 Data 中累积。
		Head.DataBlockSize = Data.GetSize();
	}
	// 流式模式（!bUsePrecache）：DataBlockSize 由 FinalizeWriteFile 根据 Writer->Tell() 计算。

	if (!bUsePrecache)
	{
		if (Writer == nullptr)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPResPatchData::EndRecord - Writer must be valid when not using precache."));
			return false;
		}
		FinalizeWriteFile();
	}
	// precache 模式：caller 通常显式调 SaveToFile（FPResPatcher::CreateBinDiff 已这样做）；
	// 但 PPakPatcher::CreatePakDiff 链路不调 → 在 PatchFilename 已设置且文件不存在时兜底落盘，
	// 避免 precache 模式产物丢盘。
	else if (!PatchFilename.IsEmpty() && !IFileManager::Get().FileExists(*PatchFilename))
	{
		if (!SaveToFile(PatchFilename))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPResPatchData::EndRecord - SaveToFile failed: %s"), *PatchFilename);
			return false;
		}
	}
	return true;
}

void FPResPatchData::AbortRecord()
{
	// 关闭 Writer（若打开）
	if (Writer)
	{
		Writer->Close();
		delete Writer;
		Writer = nullptr;
	}
	// 删除半成品文件，避免后续误读到无效 patch
	if (!PatchFilename.IsEmpty() && IFileManager::Get().FileExists(*PatchFilename))
	{
		const bool bDeleted = IFileManager::Get().Delete(*PatchFilename, /*bRequireExists*/ false);
		UE_LOG(LogPPakPacher, Display,
			TEXT("FPResPatchData::AbortRecord - Cleanup half-written patch: %s (deleted=%s)"),
			*PatchFilename, bDeleted ? TEXT("true") : TEXT("false"));
	}
	// 重置内存数据，避免误用（FPPakPatcherMemory 是 non-copyable，用 Resize(0) 代替赋值）
	Data.Resize(0);
	DataSourceType = EPPatchDataSourceType::None;
}

// -----------------------------------------------------------------------------
// Pak 便捷 Record API（委托到 PakBody）
// -----------------------------------------------------------------------------

FPPakFilePatchInfo& FPResPatchData::RecordKeep(const FString& InFileName, const FPakFile& NewPakFile, const FPakFile& OldPakFile,
	const FPakEntry& NewEntry, const FPakEntry& OldEntry, int64 InNewRealSize, int64 InOldRealSize)
{
	check(PakBody.IsValid());
	FPPakFilePatchInfo& FilePatch = PakBody->FilePatchInfos.AddDefaulted_GetRef();
	FilePatch.PatchType = EPakFilePatchType::Keep;
	FilePatch.FileName = InFileName;
	FilePatch.DataInfo.NewOffset = NewEntry.Offset;
	FilePatch.DataInfo.NewSize = NewEntry.Size;
	FilePatch.DataInfo.OldOffset = OldEntry.Offset;
	FilePatch.DataInfo.OldSize = OldEntry.Size;
	FilePatch.FileUncompressedSize = NewEntry.UncompressedSize;
	FilePatch.FileRealSize = InNewRealSize;
	FilePatch.OldFileRealSize = InOldRealSize;
	FilePatch.Entry = NewEntry;
	FilePatch.DataInfo.DataOffset = 0;
	FilePatch.DataInfo.DataSize = 0;
	return FilePatch;
}

FPPakFilePatchInfo& FPResPatchData::RecordModify(const FString& InFileName, const FPakFile& NewPakFile, const FPakFile& OldPakFile,
	const FPakEntry& NewEntry, const FPakEntry& OldEntry, const TArray<uint8>& InPatchData, int64 InNewRealSize, int64 InOldRealSize)
{
	check(PakBody.IsValid());
	FPPakFilePatchInfo& FilePatch = PakBody->FilePatchInfos.AddDefaulted_GetRef();
	FilePatch.PatchType = EPakFilePatchType::Modify;
	FilePatch.FileName = InFileName;
	FilePatch.DataInfo.NewOffset = NewEntry.Offset;
	FilePatch.DataInfo.NewSize = NewEntry.Size;
	FilePatch.DataInfo.OldOffset = OldEntry.Offset;
	FilePatch.DataInfo.OldSize = OldEntry.Size;
	FilePatch.FileUncompressedSize = NewEntry.UncompressedSize;
	FilePatch.FileRealSize = InNewRealSize;
	FilePatch.OldFileRealSize = InOldRealSize;
	FilePatch.Entry = NewEntry;
	FilePatch.DataInfo.DataSize = InPatchData.Num();
	RecordData(InPatchData.GetData(), InPatchData.Num(), FilePatch.DataInfo);
	return FilePatch;
}

FPPakFilePatchInfo& FPResPatchData::RecordModifyPerBlock(const FString& InFileName, const FPakFile& NewPakFile, const FPakFile& OldPakFile,
	const FPakEntry& NewEntry, const FPakEntry& OldEntry,
	const TArray<TArray<uint8>>& InBlockPatchDataArray, int64 InNewRealSize, int64 InOldRealSize)
{
	check(PakBody.IsValid());
	check(InBlockPatchDataArray.Num() == NewEntry.CompressionBlocks.Num());

	FPPakFilePatchInfo& FilePatch = PakBody->FilePatchInfos.AddDefaulted_GetRef();
	FilePatch.PatchType = EPakFilePatchType::Modify;
	FilePatch.FileName = InFileName;
	FilePatch.DataInfo.NewOffset = NewEntry.Offset;
	FilePatch.DataInfo.NewSize = NewEntry.Size;
	FilePatch.DataInfo.OldOffset = OldEntry.Offset;
	FilePatch.DataInfo.OldSize = OldEntry.Size;
	FilePatch.FileUncompressedSize = NewEntry.UncompressedSize;
	FilePatch.FileRealSize = InNewRealSize;
	FilePatch.OldFileRealSize = InOldRealSize;
	FilePatch.Entry = NewEntry;
	FilePatch.DataInfo.DataSize = 0; // per-block 模式 DataInfo 不携带数据

	const int32 NumBlocks = InBlockPatchDataArray.Num();
	FilePatch.BlockPatches.SetNum(NumBlocks);
	for (int32 i = 0; i < NumBlocks; ++i)
	{
		const TArray<uint8>& BlockData = InBlockPatchDataArray[i];
		FilePatch.BlockPatches[i].BlockPatchData.bIsPatchData = true;
		FilePatch.BlockPatches[i].BlockPatchData.NewSize = NewEntry.CompressionBlocks[i].CompressedEnd - NewEntry.CompressionBlocks[i].CompressedStart;
		FilePatch.BlockPatches[i].BlockPatchData.OldSize = OldEntry.CompressionBlocks[i].CompressedEnd - OldEntry.CompressionBlocks[i].CompressedStart;
		FilePatch.BlockPatches[i].BlockPatchData.NewOffset = NewEntry.CompressionBlocks[i].CompressedStart;
		FilePatch.BlockPatches[i].BlockPatchData.OldOffset = OldEntry.CompressionBlocks[i].CompressedStart;
		FilePatch.BlockPatches[i].BlockPatchData.DataSize = BlockData.Num();
		RecordData(BlockData.GetData(), BlockData.Num(), FilePatch.BlockPatches[i].BlockPatchData);
	}
	return FilePatch;
}

FPPakFilePatchInfo& FPResPatchData::RecordNew(const FString& InFileName, const FPakFile& NewPakFile, const FPakFile& OldPakFile,
	const FPakEntry& NewEntry, const FPPakMemoryArchive& InFileArchive, int64 InNewRealSize)
{
	check(PakBody.IsValid());
	FPPakFilePatchInfo& FilePatch = PakBody->FilePatchInfos.AddDefaulted_GetRef();
	FilePatch.PatchType = EPakFilePatchType::New;
	FilePatch.FileName = InFileName;
	FilePatch.DataInfo.NewOffset = NewEntry.Offset;
	FilePatch.DataInfo.NewSize = NewEntry.Size;
	FilePatch.FileUncompressedSize = NewEntry.UncompressedSize;
	FilePatch.FileRealSize = InNewRealSize;
	FilePatch.DataInfo.OldOffset = 0;
	FilePatch.DataInfo.OldSize = 0;
	FilePatch.OldFileRealSize = 0;
	FilePatch.Entry = NewEntry;
	FilePatch.DataInfo.DataSize = InFileArchive.GetSize();
	RecordData(InFileArchive.GetData(), InFileArchive.GetSize(), FilePatch.DataInfo);
	return FilePatch;
}

void FPResPatchData::RecordDataBlock(FPPakPatchDataInfo& DataInfo, int64 InNewOffset, int64 InNewSize, int64 InOldOffset, int64 InOldSize,
	uint8* InData, int64 InDataSize, bool bIsPatchData)
{
	DataInfo = FPPakPatchDataInfo();
	DataInfo.bIsPatchData = bIsPatchData;
	DataInfo.NewOffset = InNewOffset;
	DataInfo.NewSize = InNewSize;
	DataInfo.OldOffset = InOldOffset;
	DataInfo.OldSize = InOldSize;
	DataInfo.DataSize = InDataSize;
	RecordData(InData, InDataSize, DataInfo);
}

// -----------------------------------------------------------------------------
// 读取接口
// -----------------------------------------------------------------------------

bool FPResPatchData::GetFilePatchData(FPPakPatchDataInfo& FilePatchInfo, TArray<uint8>& OutCopy)
{
	if (FilePatchInfo.DataSize <= 0)
	{
		OutCopy.Reset();
		return false;
	}

	// v8: per-entry 压缩
	//   - DataSize         : 该条记录原始（解压后）字节数 — 输出 buffer 大小
	//   - CompressedSize   : 该条记录在 Data 池/磁盘上的实际占用字节
	//   - 若 CompressedSize == DataSize: 未压缩（可能 ExternalCompressType=None，或该条压缩反增不取）
	//   - 若 CompressedSize <  DataSize: 已压缩，需 Oodle 解压
	const int64 ReadSize = (FilePatchInfo.CompressedSize > 0) ? FilePatchInfo.CompressedSize : FilePatchInfo.DataSize;
	const bool bCompressed = (FilePatchInfo.CompressedSize > 0 && FilePatchInfo.CompressedSize < FilePatchInfo.DataSize);

	// 1) 取该条记录的原始（可能压缩）字节
	const uint8* SrcPtr = nullptr;	// 指向连续 ReadSize 字节
	TArray<uint8>& StreamBuf = ScratchCompressBuf;	// 流式模式临时缓冲
	if (bUsePrecache)
	{
		if (FilePatchInfo.DataOffset + ReadSize > (int64)Data.GetSize())
		{
			UE_LOG(LogPPakPacher, Error,
				TEXT("FPResPatchData::GetFilePatchData - precache out of range. Off=%lld ReadSize=%lld DataPoolSize=%lld File:%s"),
				FilePatchInfo.DataOffset, ReadSize, (int64)Data.GetSize(), *PatchFilename);
			return false;
		}
		SrcPtr = Data.GetData() + FilePatchInfo.DataOffset;
	}
	else
	{
		if (DataSourceType != EPPatchDataSourceType::Load || Reader == nullptr)
		{
			UE_LOG(LogPPakPacher, Error,
				TEXT("FPResPatchData::GetFilePatchData - streaming mode requires Reader. File:%s"), *PatchFilename);
			return false;
		}
		StreamBuf.SetNumUninitialized((int32)ReadSize, EAllowShrinking::No);
		Reader->Seek(FilePatchInfo.DataOffset);
		Reader->Serialize(StreamBuf.GetData(), ReadSize);
		if (Reader->IsError())
		{
			UE_LOG(LogPPakPacher, Error,
				TEXT("FPResPatchData::GetFilePatchData - disk read failed. Off=%lld ReadSize=%lld File:%s"),
				FilePatchInfo.DataOffset, ReadSize, *PatchFilename);
			return false;
		}
		SrcPtr = StreamBuf.GetData();
	}

	// 2) 按是否压缩分发到 OutCopy
	OutCopy.SetNumUninitialized(FilePatchInfo.DataSize);
	if (!bCompressed)
	{
		FMemory::Memcpy(OutCopy.GetData(), SrcPtr, FilePatchInfo.DataSize);
		return true;
	}

	// 该条已压缩，按 Header.ExternalCompressType 解压
	FOodleDataCompression::ECompressor Compressor;
	if (!ResolveOodleCompressor(Header.ExternalCompressType, Compressor))
	{
		UE_LOG(LogPPakPacher, Error,
			TEXT("FPResPatchData::GetFilePatchData - entry marked compressed but ExternalCompressType=%d. File:%s"),
			(int32)Header.ExternalCompressType, *PatchFilename);
		return false;
	}
	const bool bOk = FOodleDataCompression::DecompressParallel(
		OutCopy.GetData(), FilePatchInfo.DataSize,
		SrcPtr, FilePatchInfo.CompressedSize);
	if (!bOk)
	{
		UE_LOG(LogPPakPacher, Error,
			TEXT("FPResPatchData::GetFilePatchData - per-entry decompress failed. Off=%lld CompSize=%lld DataSize=%lld File:%s"),
			FilePatchInfo.DataOffset, FilePatchInfo.CompressedSize, FilePatchInfo.DataSize, *PatchFilename);
		return false;
	}
	return true;
}

// -----------------------------------------------------------------------------
// private helpers
// -----------------------------------------------------------------------------

bool FPResPatchData::SetupWriter(const FString& InFilename)
{
	if (InFilename.IsEmpty())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatchData::SetupWriter - empty filename."));
		return false;
	}
	if (Writer)
	{
		Writer->Close();
		delete Writer;
		Writer = nullptr;
	}
	Writer = IFileManager::Get().CreateFileWriter(*InFilename);
	if (Writer == nullptr)
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatchData::SetupWriter - create writer failed: %s"), *InFilename);
		return false;
	}
	return true;
}

bool FPResPatchData::SetupReader(const FString& InFilename)
{
	if (InFilename.IsEmpty())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatchData::SetupReader - empty filename."));
		return false;
	}
	if (Reader)
	{
		Reader->Close();
		delete Reader;
		Reader = nullptr;
	}
	Reader = IFileManager::Get().CreateFileReader(*InFilename);
	if (Reader == nullptr)
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPResPatchData::SetupReader - create reader failed: %s"), *InFilename);
		return false;
	}
	return true;
}

bool FPResPatchData::RecordData(const uint8* InSource, const int64 InSize, FPPakPatchDataInfo& OutDataInfo)
{
	// v8: per-entry 压缩
	//   - 若 ExternalCompressType != None 且 InSize 足够大 → 尝试 Oodle 压缩
	//   - 若压缩成功且压缩后变小 → 写入压缩字节，CompressedSize < DataSize
	//   - 否则（压缩反增 / 数据太小 / 未启用外部压缩）→ 写入原始字节，CompressedSize = DataSize

	OutDataInfo.DataSize = InSize;

	// 决策：是否尝试 per-entry 压缩
	const uint8* WritePtr = InSource;
	int64 WriteSize = InSize;

	if (InSize > 0 && Header.ExternalCompressType != EPPatchExternalCompressType::None)
	{
		FOodleDataCompression::ECompressor Compressor;
		if (ResolveOodleCompressor(Header.ExternalCompressType, Compressor))
		{
			const FOodleDataCompression::ECompressionLevel Level =
				static_cast<FOodleDataCompression::ECompressionLevel>(Header.ExternalCompressLevel);

			ScratchCompressBuf.Reset();
			TArray64<uint8> CompBuf;	// CompressParallel 需要 TArray64
			const int64 CompSz = FOodleDataCompression::CompressParallel(
				CompBuf, InSource, InSize,
				Compressor, Level,
				/*CompressIndependentChunks=*/false);
			// 仅当压缩成功且压缩后严格更小时才采用（== InSize 时不值得记录"压缩"标志）
			if (CompSz > 0 && CompSz < InSize)
			{
				ScratchCompressBuf.SetNumUninitialized((int32)CompSz, EAllowShrinking::No);
				FMemory::Memcpy(ScratchCompressBuf.GetData(), CompBuf.GetData(), CompSz);
				WritePtr  = ScratchCompressBuf.GetData();
				WriteSize = CompSz;
			}
		}
	}

	OutDataInfo.CompressedSize = WriteSize;	// == DataSize 表示该条未压缩

	if (bUsePrecache)
	{
		OutDataInfo.DataOffset = Data.GetSize();
		Data.Append(const_cast<uint8*>(WritePtr), WriteSize);
	}
	else
	{
		OutDataInfo.DataOffset = Writer->Tell();
		Writer->Serialize(const_cast<uint8*>(WritePtr), WriteSize);
	}
	return true;
}

bool FPResPatchData::FinalizeWriteFile()
{
	if (Writer)
	{
		// v8: DataBlockSize 直接是磁盘上 DataBlock 字节数（无论是否启用外部压缩）。
		// per-entry 压缩信息已经在每条 FPPakPatchDataInfo 内（CompressedSize 字段）。
		Head.DataBlockSize = Writer->Tell() - Head.DataBlockOffset;

		// step 2: write Header + Body
		{
			Head.IndexOffset = Writer->Tell();
			Header.Serialize(*Writer);
			SerializeBody(*Writer);
			Head.IndexSize = Writer->Tell() - Head.IndexOffset;
		}

		// step 3: write Head + HeadOffset
		{
			HeadOffset = Writer->Tell();
			Head.Serialize(*Writer);
			(*Writer) << HeadOffset;
		}

		Writer->Close();
		delete Writer;
		Writer = nullptr;
		return true;
	}
	return false;
}

void FPResPatchData::SerializeBody(FArchive& Ar)
{
	switch (Header.Type)
	{
	case EPResPatchType::Bin:
		check(BinBody.IsValid());
		BinBody->Serialize(Ar);
		break;
	case EPResPatchType::Pak:
	{
		check(PakBody.IsValid());
		PakBody->Serialize(Ar);

		// Pak 类型可能携带 IoStore 同伴 body（UE5 cook 产物：.pak + .utoc + .ucas 三件套）
		// 由 FPIoStorePatcher::CreateIoStoreDiff 在 Pak 主流程末尾按需分配。
		// 这里用 bool 标志位序列化，让读侧也能正确按需分配 IoStoreBody。
		bool bHasIoStoreCompanion = IoStoreBody.IsValid();
		Ar << bHasIoStoreCompanion;
		if (Ar.IsLoading() && bHasIoStoreCompanion && !IoStoreBody.IsValid())
		{
			IoStoreBody = MakeUnique<FPIoStorePatchBody>();
		}
		if (bHasIoStoreCompanion)
		{
			check(IoStoreBody.IsValid());
			IoStoreBody->Serialize(Ar);
		}
		break;
	}
	case EPResPatchType::IoStore:
		check(IoStoreBody.IsValid());
		IoStoreBody->Serialize(Ar);
		break;
	case EPResPatchType::None:
	default:
		// 不序列化任何内容
		break;
	}
}

void FPResPatchData::EnsureBodyAllocated()
{
	switch (Header.Type)
	{
	case EPResPatchType::Bin:
		if (!BinBody.IsValid()) { BinBody = MakeUnique<FPBinPatchBody>(); }
		break;
	case EPResPatchType::Pak:
		if (!PakBody.IsValid()) { PakBody = MakeUnique<FPPakPatchBody>(); }
		break;
	case EPResPatchType::IoStore:
		if (!IoStoreBody.IsValid()) { IoStoreBody = MakeUnique<FPIoStorePatchBody>(); }
		break;
	default:
		break;
	}
}
