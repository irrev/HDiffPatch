#include "Data/PPakPatchBody.h"
#include "Data/PPakPatcherDataType.h"

//#define PATCH_ENSURE
#define PATCH_ENSURE ensure

// -----------------------------------------------------------------------------
// FPPakPatchDataInfo
// -----------------------------------------------------------------------------

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

// -----------------------------------------------------------------------------
// FPPakFilePatchInfo
// -----------------------------------------------------------------------------

void FPPakFilePatchInfo::Serialize(FArchive& Ar)
{
	Ar << FileName;
	Ar << FileUncompressedSize;
	Ar << FileRealSize;
	Ar << OldFileRealSize;
	Ar << PatchType;
	DataInfo.Serialize(Ar);
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

// -----------------------------------------------------------------------------
// FPPakPatchBody
// -----------------------------------------------------------------------------

void FPPakPatchBody::Serialize(FArchive& Ar)
{
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

	for (int32 i = 0; i < FileInfoNum; ++i)
	{
		FilePatchInfos[i].Serialize(Ar);
	}

	IndexPatchInfo.Serialize(Ar);

	Ar << bHasPathHashIndex;
	PathHashPatchInfo.Serialize(Ar);
	Ar << PathHashIndexHash;

	Ar << bHasFullDirectoryIndex;
	FullDirectoryPatchInfo.Serialize(Ar);
	Ar << FullDirectoryIndexHash;

	HeadPatchInfo.Serialize(Ar);

	SignFileInfo.Serialize(Ar);

	Ar << IndexHash;
	Ar << bSign;
	Ar << MountPoint;
}

bool FPPakPatchBody::IsEqual(FPPakPatchBody& Other)
{
	bool bIsEqual = true;
	bIsEqual &= PATCH_ENSURE(this->FilePatchInfos.Num() == Other.FilePatchInfos.Num());
	for (int32 i = 0; i < FilePatchInfos.Num() && i < Other.FilePatchInfos.Num(); ++i)
	{
		bIsEqual &= FilePatchInfos[i].IsEqual(Other.FilePatchInfos[i]);
	}
	bIsEqual &= PATCH_ENSURE(IndexPatchInfo.IsEqual(Other.IndexPatchInfo));
	bIsEqual &= PATCH_ENSURE(bHasPathHashIndex == Other.bHasPathHashIndex);
	bIsEqual &= PATCH_ENSURE(PathHashPatchInfo.IsEqual(Other.PathHashPatchInfo));
	bIsEqual &= PATCH_ENSURE(PathHashIndexHash == Other.PathHashIndexHash);
	bIsEqual &= PATCH_ENSURE(bHasFullDirectoryIndex == Other.bHasFullDirectoryIndex);
	bIsEqual &= PATCH_ENSURE(FullDirectoryPatchInfo.IsEqual(Other.FullDirectoryPatchInfo));
	bIsEqual &= PATCH_ENSURE(FullDirectoryIndexHash == Other.FullDirectoryIndexHash);
	bIsEqual &= PATCH_ENSURE(HeadPatchInfo.IsEqual(Other.HeadPatchInfo));
	bIsEqual &= PATCH_ENSURE(SignFileInfo.IsEqual(Other.SignFileInfo));
	bIsEqual &= PATCH_ENSURE(IndexHash == Other.IndexHash);
	bIsEqual &= PATCH_ENSURE(bSign == Other.bSign);
	bIsEqual &= PATCH_ENSURE(MountPoint == Other.MountPoint);
	return bIsEqual;
}
