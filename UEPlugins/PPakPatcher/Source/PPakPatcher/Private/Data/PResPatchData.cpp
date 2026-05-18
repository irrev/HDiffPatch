#include "Data/PResPatchData.h"
#include "Data/PPakPatcherDataType.h"
#include "PPakPatcherSettings.h"

//#define PATCH_ENSURE
#define PATCH_ENSURE ensure

// -----------------------------------------------------------------------------
// FPResPatchHead
// -----------------------------------------------------------------------------

void FPResPatchHead::Serialize(FArchive& Ar)
{
	Ar << DataBlockOffset;
	Ar << DataBlockSize;
	Ar << IndexOffset;
	Ar << IndexSize;
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

	uint8 PreprocessByte = static_cast<uint8>(PakAwarePreprocess);
	Ar << PreprocessByte;
	if (Ar.IsLoading())
	{
		PakAwarePreprocess = static_cast<EPPakAwarePreprocess>(PreprocessByte);
	}

	uint8 GenerateModeByte = static_cast<uint8>(GenerateMode);
	Ar << GenerateModeByte;
	if (Ar.IsLoading())
	{
		GenerateMode = static_cast<EPPakPatchMode>(GenerateModeByte);
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
	bIsEqual &= PATCH_ENSURE(PakAwarePreprocess == Other.PakAwarePreprocess);
	bIsEqual &= PATCH_ENSURE(GenerateMode == Other.GenerateMode);
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

	// step 1: write data body first.
	{
		Head.DataBlockOffset = Writer->Tell();
		Writer->Serialize(Data.GetData(), Data.GetSize());
		Head.DataBlockSize = Writer->Tell() - Head.DataBlockOffset;
	}

	FinalizeWriteFile();
	return true;
}

bool FPResPatchData::LoadFromFile(const FString& InPatchFilename)
{
	PatchFilename = InPatchFilename;
	FPaths::MakeStandardFilename(PatchFilename);

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
		Reader = nullptr;
		return false;
	}

	EnsureBodyAllocated();
	SerializeBody(*Reader);

	// step 4: DataBlock
	if (bUsePrecache)
	{
		Reader->Seek(Head.DataBlockOffset);
		Data.Resize(Head.DataBlockSize);
		Reader->Serialize(Data.GetData(), Data.GetSize());

		Reader->Close();
		Reader = nullptr;
	}

	return true;
}

// -----------------------------------------------------------------------------
// Begin / End Record
// -----------------------------------------------------------------------------

bool FPResPatchData::BeginRecord(const FString& InPatchFilename, EPResPatchType InType,
	const FString& InOldFileName, const FString& InNewFileName,
	const FString& InOldMD5, const FString& InNewMD5,
	uint32 InOldCrc32, uint32 InNewCrc32,
	EPakPatchCompressType InCompressType)
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

	// 按 Type 分配 Body
	PakBody.Reset();
	BinBody.Reset();
	IoStoreBody.Reset();
	EnsureBodyAllocated();

	DataSourceType = EPPatchDataSourceType::Record;
	bUsePrecache = UPPakPatcherSettings::Get().bPrecachePatchDataOnSave;

	if (!bUsePrecache)
	{
		PatchFilename = InPatchFilename;
		FPaths::MakeStandardFilename(PatchFilename);
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
	Head.DataBlockSize = Data.GetSize();
	if (!bUsePrecache)
	{
		if (Writer == nullptr)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPResPatchData::EndRecord - Writer must be valid when not using precache."));
			return false;
		}
		FinalizeWriteFile();
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

uint8* FPResPatchData::GetFilePatchData(FPPakPatchDataInfo& FilePatchInfo)
{
	if (FilePatchInfo.DataSize > 0)
	{
		if (bUsePrecache)
		{
			return Data.GetData() + FilePatchInfo.DataOffset;
		}
		else if (DataSourceType == EPPatchDataSourceType::Load)
		{
			Data.Reserve(FilePatchInfo.DataSize);
			Reader->Seek(FilePatchInfo.DataOffset);
			Reader->Serialize(Data.GetData(), FilePatchInfo.DataSize);
			return Data.GetData();
		}
	}
	return nullptr;
}

bool FPResPatchData::GetFilePatchData(FPPakPatchDataInfo& FilePatchInfo, TArray<uint8>& OutCopy)
{
	if (FilePatchInfo.DataSize > 0)
	{
		if (bUsePrecache)
		{
			if (FilePatchInfo.DataOffset + FilePatchInfo.DataSize <= Data.GetSize())
			{
				OutCopy.SetNumUninitialized(FilePatchInfo.DataSize);
				FMemory::Memcpy(OutCopy.GetData(), Data.GetData() + FilePatchInfo.DataOffset, FilePatchInfo.DataSize);
				return true;
			}
		}
		else if (DataSourceType == EPPatchDataSourceType::Load)
		{
			OutCopy.SetNumUninitialized(FilePatchInfo.DataSize);
			Reader->Seek(FilePatchInfo.DataOffset);
			Reader->Serialize(OutCopy.GetData(), FilePatchInfo.DataSize);
			return true;
		}
	}
	return false;
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
	if (bUsePrecache)
	{
		OutDataInfo.DataOffset = Data.GetSize();
		OutDataInfo.DataSize = InSize;
		Data.Append(const_cast<uint8*>(InSource), InSize);
		return true;
	}
	else
	{
		OutDataInfo.DataOffset = Writer->Tell();
		OutDataInfo.DataSize = InSize;
		Writer->Serialize(const_cast<uint8*>(InSource), InSize);
		return true;
	}
}

bool FPResPatchData::FinalizeWriteFile()
{
	if (Writer)
	{
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
