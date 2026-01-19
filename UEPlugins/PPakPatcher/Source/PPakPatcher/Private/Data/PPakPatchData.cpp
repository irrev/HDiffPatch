#include "Data/PPakPatchData.h"
#include "Data/PPakPacherDataType.h"
#include "PPakPatcherSettings.h"
#include "Data/PPakFileData.h"

//#define PATCH_ENSURE
#define PATCH_ENSURE ensure

void FPPakPatchDataInfo::Serialize(FArchive& Ar)
{
	Ar << bIsPatchData;
	Ar << NewOffset;
	Ar << NewSize;
	Ar << OldOffset;
	Ar << OldSize;
	Ar << DataOffset;
	Ar << DataSize;
}


bool FPPakPatchDataInfo::IsEqual(FPPakPatchDataInfo& Other)
{
	bool bIsEqual = true;

	bIsEqual &= PATCH_ENSURE(this->bIsPatchData == Other.bIsPatchData);
	bIsEqual &= PATCH_ENSURE(this->NewOffset == Other.NewOffset);
	bIsEqual &= PATCH_ENSURE(this->NewSize == Other.NewSize);
	bIsEqual &= PATCH_ENSURE(this->OldOffset == Other.OldOffset);
	bIsEqual &= PATCH_ENSURE(this->OldSize == Other.OldSize);
	bIsEqual &= PATCH_ENSURE(this->DataOffset == Other.DataOffset);
	bIsEqual &= PATCH_ENSURE(this->DataSize == Other.DataSize);

	return bIsEqual;
}

void FPPakFilePatchInfo::Serialize(FArchive& Ar)
{
	Ar << FileName;
	Ar << FileUncompressedSize;
	Ar << FileRealSize;
	Ar << OldFileRealSize;
	Ar << PatchType;
	DataInfo.Serialize(Ar);

	//Entry.Serialize(Ar, FPakInfo::PakFile_Version_Latest);
}


bool FPPakFilePatchInfo::IsEqual(FPPakFilePatchInfo& Other)
{
	bool bIsEqual = true;

	bIsEqual &= PATCH_ENSURE(this->FileName == Other.FileName);
	bIsEqual &= PATCH_ENSURE(this->FileUncompressedSize == Other.FileUncompressedSize);
	bIsEqual &= PATCH_ENSURE(this->FileRealSize == Other.FileRealSize);
	bIsEqual &= PATCH_ENSURE(this->OldFileRealSize == Other.OldFileRealSize);
	bIsEqual &= PATCH_ENSURE(this->PatchType == Other.PatchType);
	bIsEqual &= PATCH_ENSURE(this->DataInfo.IsEqual(Other.DataInfo));

	return bIsEqual;
}

void FPPakPatchInfo::Serialize(FArchive& Ar)
{
	Ar << NewPakName;
	Ar << NewPakHash;
	Ar << NewVersion;
	Ar << OldPakName;
	Ar << OldPakHash;
	Ar << OldVersion;

	int32 FileInfoNum = 0;
	if (Ar.IsLoading())
	{
		Ar << FileInfoNum;
		FilePatchInfos.SetNumZeroed(FileInfoNum);
	}
	else
	{
		FileInfoNum = FilePatchInfos.Num();
		Ar << FileInfoNum;
	}

	for (int32 i=0; i< FileInfoNum; ++i)
	{
		FilePatchInfos[i].Serialize(Ar);
	}

	IndexPatchInfo.Serialize(Ar);
	PathPatchInfo.Serialize(Ar);
	HeadPatchInfo.Serialize(Ar);

	SignFileInfo.Serialize(Ar);

	Ar << IndexHash;
	Ar << bSign;
	Ar << MountPoint;
}


bool FPPakPatchInfo::IsEqual(FPPakPatchInfo& Other)
{
	bool bIsEqual = true;

	bIsEqual &= PATCH_ENSURE(this->NewPakName == Other.NewPakName);
	bIsEqual &= PATCH_ENSURE(this->NewPakHash == Other.NewPakHash);
	bIsEqual &= PATCH_ENSURE(this->NewVersion == Other.NewVersion);
	bIsEqual &= PATCH_ENSURE(this->OldPakName == Other.OldPakName);
	bIsEqual &= PATCH_ENSURE(this->OldPakHash == Other.OldPakHash);
	bIsEqual &= PATCH_ENSURE(this->OldVersion == Other.OldVersion);
	bIsEqual &= PATCH_ENSURE(this->FilePatchInfos.Num() == Other.FilePatchInfos.Num());

	for (int32 i = 0; i < FilePatchInfos.Num(); ++i)
	{
		bIsEqual &= FilePatchInfos[i].IsEqual(Other.FilePatchInfos[i]);
	}
	return bIsEqual;
}

void FPPakPatchHead::Serialize(FArchive& Ar)
{
	Ar << DataBlockOffset;
	Ar << DataBlockSize;
	Ar << IndexOffset;
	Ar << IndexSize;
}


bool FPPakPatchHead::IsEqual(FPPakPatchHead& Other)
{
	bool bIsEqual = true;

	bIsEqual &= PATCH_ENSURE(this->DataBlockOffset == Other.DataBlockOffset);
	bIsEqual &= PATCH_ENSURE(this->DataBlockSize == Other.DataBlockSize);
	bIsEqual &= PATCH_ENSURE(this->IndexOffset == Other.IndexOffset);
	bIsEqual &= PATCH_ENSURE(this->IndexSize == Other.IndexSize);

	return bIsEqual;
}

FPPakPatchData::FPPakPatchData()
{
}

FPPakPatchData::~FPPakPatchData()
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

bool FPPakPatchData::IsEqual(FPPakPatchData& Other)
{
	bool bIsEqual = true;
	bIsEqual &= PATCH_ENSURE(this->Info.IsEqual(Other.Info));
	bIsEqual &= PATCH_ENSURE(this->Head.IsEqual(Other.Head));
	bIsEqual &= PATCH_ENSURE(this->HeadOffset == Other.HeadOffset);
	if(this->bUsePrecache && Other.bUsePrecache)
	{
		bIsEqual &= FMemory::Memcmp(this->Data.GetData(), Other.Data.GetData(), this->Data.GetSize()) == 0;
	}

	return bIsEqual;
}

bool FPPakPatchData::SaveToFile(const FString& InPatchFilename)
{
	if (!bUsePrecache)
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatchData::SaveToFile. This function only support at when use precache memory. file: %s"), *PatchFilename);
		return false;
	}
	PatchFilename = InPatchFilename;
	if (PatchFilename.IsEmpty())
	{
		PatchFilename = FPaths::ChangeExtension(Info.OldPakName, TEXT(".patch"));
	}

	FPaths::MakeStandardFilename(PatchFilename);
	if (!SetupWriter(PatchFilename))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatchData::SaveToFile - fail by create writer fail."));
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

bool FPPakPatchData::LoadFromFile(const FString& InPatchFilename)
{
	PatchFilename = InPatchFilename;
	FPaths::MakeStandardFilename(PatchFilename);

	// setup reader
	if (!SetupReader(PatchFilename))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatchData::LoadFromFile - fail by create reader fail."));
		return false;
	}

	DataSourceType = EPPatchDataSourceType::Load;
	bUsePrecache = FPPakPatcherSettings::Get().bPrecachePatchDataOnLoad;

	// step 1: read head offset.
	Reader->Seek(Reader->TotalSize() - sizeof(HeadOffset));
	(*Reader) << HeadOffset;

	// step 2: load head
	Reader->Seek(HeadOffset);
	Head.Serialize(*Reader);

	// step 3: load info
	Reader->Seek(Head.IndexOffset);
	Info.Serialize(*Reader);

	// step 4: load data.
	if (bUsePrecache)
	{
		// if use pre-cache memory, load all data to memory and no longer need handling file reader.		
		Reader->Seek(Head.DataBlockOffset);
		Data.Resize(Head.DataBlockSize);
		Reader->Serialize(Data.GetData(), Data.GetSize());

		Reader->Close();
		Reader = nullptr;
	}

	return true;
}

bool FPPakPatchData::BeginRecord(const FString& InPatchFilename, const FPPakFileDataPtr& InNewPak, const FPPakFileDataPtr& InOldPak)
{
	Info = FPPakPatchInfo();
	Info.NewPakName = InNewPak->PakFilename;
	Info.NewPakHash = InNewPak->GetPakFileMD5();
	Info.NewVersion = InNewPak->PakInfo.Version;
	Info.OldPakName = InOldPak->PakFilename;
	Info.OldPakHash = InOldPak->GetPakFileMD5();
	Info.OldVersion = InOldPak->PakInfo.Version;
	//Info.PakInfo = InNewPak->PakInfo;
	Info.bSign = InNewPak->bIsSigned;
	Info.MountPoint = InNewPak->PakFilePtr->GetMountPoint();

	DataSourceType = EPPatchDataSourceType::Record;
	bUsePrecache = FPPakPatcherSettings::Get().bPrecachePatchDataOnSave;

	if (!bUsePrecache)
	{
		PatchFilename = InPatchFilename;
		FPaths::MakeStandardFilename(PatchFilename);
		if (!SetupWriter(PatchFilename))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatchData::BeginRecord - fail by create writer fail."));
			return false;
		}
		Head.DataBlockOffset = Writer->Tell();
	}

	return true;
}

FPPakFilePatchInfo& FPPakPatchData::RecordKeep(const FString& InFileName, const FPakFile& NewPakFile, const FPakFile& OldPakFile,
	const FPakEntry& NewEntry, const FPakEntry& OldEntry, int64 InNewRealSize, int64 InOldRealSize)
{
	FPPakFilePatchInfo& FilePatch = Info.FilePatchInfos.AddDefaulted_GetRef();
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

FPPakFilePatchInfo& FPPakPatchData::RecordModify(const FString& InFileName, const FPakFile& NewPakFile, const FPakFile& OldPakFile,
	const FPakEntry& NewEntry, const FPakEntry& OldEntry, const TArray<uint8>& InPatchData, int64 InNewRealSize, int64 InOldRealSize)
{
	FPPakFilePatchInfo& FilePatch = Info.FilePatchInfos.AddDefaulted_GetRef();
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

FPPakFilePatchInfo& FPPakPatchData::RecordNew(const FString& InFileName, const FPakFile& NewPakFile, const FPakFile& OldPakFile, const FPakEntry& NewEntry,
	const FPPakMemoryArchive& InFileArchive, int64 InNewRealSize)
{
	FPPakFilePatchInfo& FilePatch = Info.FilePatchInfos.AddDefaulted_GetRef();
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

void FPPakPatchData::RecordIndexBlock(int64 InNewOffset, int64 InNewSize, int64 InOldOffset, int64 InOldSize, uint8* InData, int64 InDataSize, bool bIsPatchData)
{
	Info.IndexPatchInfo = FPPakPatchDataInfo();

	Info.IndexPatchInfo.bIsPatchData = bIsPatchData;
	Info.IndexPatchInfo.NewOffset = InNewOffset;
	Info.IndexPatchInfo.NewSize = InNewSize;
	Info.IndexPatchInfo.OldOffset = InOldOffset;
	Info.IndexPatchInfo.OldSize = InOldSize;

	Info.IndexPatchInfo.DataSize = InDataSize;
	RecordData(InData, InDataSize, Info.IndexPatchInfo);
}

void FPPakPatchData::RecordPathBlock(int64 InNewOffset, int64 InNewSize, int64 InOldOffset, int64 InOldSize, uint8* InData, int64 InDataSize, bool bIsPatchData)
{
	Info.PathPatchInfo = FPPakPatchDataInfo();

	Info.PathPatchInfo.bIsPatchData = bIsPatchData;
	Info.PathPatchInfo.NewOffset = InNewOffset;
	Info.PathPatchInfo.NewSize = InNewSize;
	Info.PathPatchInfo.OldOffset = InOldOffset;
	Info.PathPatchInfo.OldSize = InOldSize;

	Info.PathPatchInfo.DataSize = InDataSize;
	RecordData(InData, InDataSize, Info.PathPatchInfo);
}

void FPPakPatchData::RecordHeadBlock(int64 InNewOffset, int64 InNewSize, int64 InOldOffset, int64 InOldSize, uint8* InData, int64 InDataSize, bool bIsPatchData)
{
	Info.HeadPatchInfo = FPPakPatchDataInfo();

	Info.HeadPatchInfo.bIsPatchData = bIsPatchData;
	Info.HeadPatchInfo.NewOffset = InNewOffset;
	Info.HeadPatchInfo.NewSize = InNewSize;
	Info.HeadPatchInfo.OldOffset = InOldOffset;
	Info.HeadPatchInfo.OldSize = InOldSize;

	Info.HeadPatchInfo.DataSize = InDataSize;
	RecordData(InData, InDataSize, Info.HeadPatchInfo);
}


void FPPakPatchData::RecordDataBlock(FPPakPatchDataInfo& DataInfo, int64 InNewOffset, int64 InNewSize, int64 InOldOffset, int64 InOldSize, uint8* InData, int64 InDataSize, bool bIsPatchData)
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

bool FPPakPatchData::EndRecord()
{
	Head.DataBlockSize = Data.GetSize();
	if (!bUsePrecache)
	{
		if (Writer == nullptr)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatchData::EndRecord - if not use precache Writer must be valid when end record."));
			return false;
		}
		FinalizeWriteFile();
	}

	return true;
}

uint8* FPPakPatchData::GetFilePatchData(FPPakPatchDataInfo& FilePatchInfo)
{
	if (FilePatchInfo.DataSize > 0)
	{
		if (bUsePrecache)
		{
			// read from pre-cached Data.
			return Data.GetData() + FilePatchInfo.DataOffset;
		}
		else if(DataSourceType == EPPatchDataSourceType::Load)
		{
			// read from file. can save memory.
			Data.Reserve(FilePatchInfo.DataSize);
			Reader->Seek(FilePatchInfo.DataOffset);
			Reader->Serialize(Data.GetData(), FilePatchInfo.DataSize);
			return Data.GetData();
		}
	}
	
	return nullptr;
}


bool FPPakPatchData::GetFilePatchData(FPPakPatchDataInfo& FilePatchInfo, TArray<uint8>& OutCopy)
{
	if (FilePatchInfo.DataSize > 0)
	{
		if (bUsePrecache)
		{
			// read from pre-cached Data.
			if (FilePatchInfo.DataOffset + FilePatchInfo.DataSize <= Data.GetSize())
			{
				OutCopy.SetNumUninitialized(FilePatchInfo.DataSize);
				FMemory::Memcpy(OutCopy.GetData(), Data.GetData() + FilePatchInfo.DataOffset, FilePatchInfo.DataSize);
				return true;
			}
		}
		else if (DataSourceType == EPPatchDataSourceType::Load)
		{
			// read from file. can save memory.
			OutCopy.SetNumUninitialized(FilePatchInfo.DataSize);
			Reader->Seek(FilePatchInfo.DataOffset);
			Reader->Serialize(OutCopy.GetData(), FilePatchInfo.DataSize);
			return true;
		}
	}

	return false;
}


bool FPPakPatchData::SetupWriter(const FString& InFilename)
{
	if (InFilename.IsEmpty())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("Fail to create pak patch writer. InFilename was Empty."));
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
		UE_LOG(LogPPakPacher, Error, TEXT("Fail to create pak patch writer. file: %s"), *InFilename);
		return false;
	}
	return true;
}


bool FPPakPatchData::SetupReader(const FString& InFilename)
{
	if (InFilename.IsEmpty())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("Fail to create pak patch reader. InFilename was Empty."));
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
		UE_LOG(LogPPakPacher, Error, TEXT("Fail to create pak patch reader. file: %s"), *InFilename);
		return false;
	}
	return true;
}

bool FPPakPatchData::RecordData(const uint8* InSource, const int64 InSize, FPPakPatchDataInfo& OutDataInfo)
{
	if (bUsePrecache)
	{
		// write to Data (large memory)
		OutDataInfo.DataOffset = Data.GetSize();
		OutDataInfo.DataSize = InSize;
		Data.Append(const_cast<uint8*>(InSource), InSize);
		return true;
	}
	else
	{
		// write to file.
		OutDataInfo.DataOffset = Writer->Tell();
		OutDataInfo.DataSize = InSize;
		Writer->Serialize(const_cast<uint8*>(InSource), InSize);
		return true;
	}
	return false;
}

bool FPPakPatchData::FinalizeWriteFile()
{
	if (Writer)
	{
		Head.DataBlockSize = Writer->Tell() - Head.DataBlockOffset;

		// step 2: write info
		{
			Head.IndexOffset = Writer->Tell();
			Info.Serialize(*Writer);
			Head.IndexSize = Writer->Tell() - Head.IndexOffset;
		}

		// step 3: write head
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

