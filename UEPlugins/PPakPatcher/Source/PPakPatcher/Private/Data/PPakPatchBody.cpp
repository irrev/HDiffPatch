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
	Ar << CompressedSize;	// v8: per-entry 压缩字段
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
	bIsEqual &= PATCH_ENSURE(this->CompressedSize == Other.CompressedSize);
	return bIsEqual;
}

// -----------------------------------------------------------------------------
// FPPakBlockPatchInfo (v6)
// -----------------------------------------------------------------------------

void FPPakBlockPatchInfo::Serialize(FArchive& Ar)
{
	BlockPatchData.Serialize(Ar);
}

bool FPPakBlockPatchInfo::IsEqual(FPPakBlockPatchInfo& Other)
{
	return BlockPatchData.IsEqual(Other.BlockPatchData);
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

	// Entry 关键字段序列化：DAC 路径需要 Flags + CompressionMethodIndex + NumBlocks +
	// CompressionBlocks（per-block 路径必需）+ CompressionBlockSize + Size + UncompressedSize + Hash + Offset。
	// v6 起补充 Hash + Offset 是因为 per-block 路径需要重建 EntryHeader（不再通过 HDiff 还原）。
	{
		uint8 Flags = Entry.Flags;
		uint32 CompMethodIdx = Entry.CompressionMethodIndex;
		int32 NumBlocks = Entry.CompressionBlocks.Num();
		uint32 CompBlockSize = Entry.CompressionBlockSize;
		int64 EntrySize = Entry.Size;
		int64 EntryUncompressedSize = Entry.UncompressedSize;
		int64 EntryOffset = Entry.Offset;

		Ar << Flags;
		Ar << CompMethodIdx;
		Ar << NumBlocks;
		Ar << CompBlockSize;
		Ar << EntrySize;
		Ar << EntryUncompressedSize;
		Ar << EntryOffset;
		// Hash 20 字节
		Ar.Serialize(Entry.Hash, sizeof(Entry.Hash));

		if (Ar.IsSaving())
		{
			for (int32 i = 0; i < NumBlocks; ++i)
			{
				Ar << Entry.CompressionBlocks[i].CompressedStart;
				Ar << Entry.CompressionBlocks[i].CompressedEnd;
			}
		}

		if (Ar.IsLoading())
		{
			Entry.Flags = Flags;
			Entry.CompressionMethodIndex = CompMethodIdx;
			Entry.CompressionBlockSize = CompBlockSize;
			Entry.Size = EntrySize;
			Entry.UncompressedSize = EntryUncompressedSize;
			Entry.Offset = EntryOffset;
			Entry.CompressionBlocks.SetNum(NumBlocks);
			for (int32 i = 0; i < NumBlocks; ++i)
			{
				Ar << Entry.CompressionBlocks[i].CompressedStart;
				Ar << Entry.CompressionBlocks[i].CompressedEnd;
			}
		}
	}

	// v6: BlockPatches 数组（非空表示 per-block 模式）
	int32 NumBlockPatches = BlockPatches.Num();
	Ar << NumBlockPatches;
	if (Ar.IsLoading())
	{
		BlockPatches.SetNum(NumBlockPatches);
	}
	for (int32 i = 0; i < NumBlockPatches; ++i)
	{
		BlockPatches[i].Serialize(Ar);
	}
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
	bIsEqual &= PATCH_ENSURE(this->BlockPatches.Num() == Other.BlockPatches.Num());
	for (int32 i = 0; i < BlockPatches.Num() && i < Other.BlockPatches.Num(); ++i)
	{
		bIsEqual &= BlockPatches[i].IsEqual(Other.BlockPatches[i]);
	}
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
