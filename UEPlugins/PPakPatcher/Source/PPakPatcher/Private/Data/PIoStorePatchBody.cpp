#include "Data/PIoStorePatchBody.h"

#define PATCH_ENSURE ensure

// ---------------------------------------------------------------------------
// FPIoStoreChunkPatchInfo
// ---------------------------------------------------------------------------

void FPIoStoreChunkPatchInfo::Serialize(FArchive& Ar)
{
	// FIoChunkId 是 12 字节 POD
	Ar.Serialize(&ChunkId, sizeof(FIoChunkId));

	uint8 TypeByte = static_cast<uint8>(PatchType);
	Ar << TypeByte;
	if (Ar.IsLoading()) PatchType = static_cast<EIoStoreChunkPatchType>(TypeByte);

	Ar << OldOffset;
	Ar << OldLength;
	Ar << NewOffset;
	Ar << NewLength;

	DataInfo.Serialize(Ar);
}

// ---------------------------------------------------------------------------
// FPIoStorePatchBody
// ---------------------------------------------------------------------------

void FPIoStorePatchBody::Serialize(FArchive& Ar)
{
	uint8 StrategyByte = static_cast<uint8>(Strategy);
	Ar << StrategyByte;
	if (Ar.IsLoading())
	{
		Strategy = static_cast<EIoStoreDiffStrategy>(StrategyByte);
	}

	// .utoc 整体 diff（两种策略均有）
	UtocDiffInfo.Serialize(Ar);
	Ar << OldUtocMD5;
	Ar << NewUtocMD5;
	Ar << OldUtocCrc32;
	Ar << NewUtocCrc32;

	if (Strategy == EIoStoreDiffStrategy::FileBinary)
	{
		// .ucas 整体 diff
		UcasDiffInfo.Serialize(Ar);
		Ar << OldUcasMD5;
		Ar << NewUcasMD5;
		Ar << OldUcasCrc32;
		Ar << NewUcasCrc32;
	}
	else if (Strategy == EIoStoreDiffStrategy::ChunkAware)
	{
		// .ucas hash（用于校验）
		Ar << OldUcasMD5;
		Ar << NewUcasMD5;
		Ar << OldUcasCrc32;
		Ar << NewUcasCrc32;

		// .ucas 文件大小
		Ar << OldUcasFileSize;
		Ar << NewUcasFileSize;

		// per-chunk patch info
		int32 NumChunks = ChunkPatchInfos.Num();
		Ar << NumChunks;
		if (Ar.IsLoading())
		{
			ChunkPatchInfos.SetNum(NumChunks);
		}
		for (int32 i = 0; i < NumChunks; ++i)
		{
			ChunkPatchInfos[i].Serialize(Ar);
		}
	}
}

bool FPIoStorePatchBody::IsEqual(FPIoStorePatchBody& Other)
{
	bool bIsEqual = true;
	bIsEqual &= PATCH_ENSURE(Strategy == Other.Strategy);
	bIsEqual &= PATCH_ENSURE(UtocDiffInfo.IsEqual(Other.UtocDiffInfo));
	bIsEqual &= PATCH_ENSURE(OldUtocMD5 == Other.OldUtocMD5);
	bIsEqual &= PATCH_ENSURE(NewUtocMD5 == Other.NewUtocMD5);
	bIsEqual &= PATCH_ENSURE(OldUtocCrc32 == Other.OldUtocCrc32);
	bIsEqual &= PATCH_ENSURE(NewUtocCrc32 == Other.NewUtocCrc32);

	if (Strategy == EIoStoreDiffStrategy::FileBinary)
	{
		bIsEqual &= PATCH_ENSURE(UcasDiffInfo.IsEqual(Other.UcasDiffInfo));
	}

	bIsEqual &= PATCH_ENSURE(OldUcasMD5 == Other.OldUcasMD5);
	bIsEqual &= PATCH_ENSURE(NewUcasMD5 == Other.NewUcasMD5);
	bIsEqual &= PATCH_ENSURE(OldUcasCrc32 == Other.OldUcasCrc32);
	bIsEqual &= PATCH_ENSURE(NewUcasCrc32 == Other.NewUcasCrc32);

	if (Strategy == EIoStoreDiffStrategy::ChunkAware)
	{
		bIsEqual &= PATCH_ENSURE(ChunkPatchInfos.Num() == Other.ChunkPatchInfos.Num());
	}
	return bIsEqual;
}
