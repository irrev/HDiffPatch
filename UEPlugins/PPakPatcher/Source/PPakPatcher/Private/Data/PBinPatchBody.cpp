#include "Data/PBinPatchBody.h"

#define PATCH_ENSURE ensure

void FPBinPatchBody::Serialize(FArchive& Ar)
{
	DiffInfo.Serialize(Ar);

	Ar << bHasUtocDiff;
	if (bHasUtocDiff)
	{
		UtocDiffInfo.Serialize(Ar);
		Ar << UtocOldSize;
		Ar << UtocNewSize;
	}

	Ar << bHasUcasDiff;
	if (bHasUcasDiff)
	{
		UcasDiffInfo.Serialize(Ar);
		Ar << UcasOldSize;
		Ar << UcasNewSize;
	}
}

bool FPBinPatchBody::IsEqual(FPBinPatchBody& Other)
{
	bool bIsEqual = true;
	bIsEqual &= PATCH_ENSURE(DiffInfo.IsEqual(Other.DiffInfo));
	bIsEqual &= PATCH_ENSURE(bHasUtocDiff == Other.bHasUtocDiff);
	if (bHasUtocDiff) bIsEqual &= PATCH_ENSURE(UtocDiffInfo.IsEqual(Other.UtocDiffInfo));
	bIsEqual &= PATCH_ENSURE(bHasUcasDiff == Other.bHasUcasDiff);
	if (bHasUcasDiff) bIsEqual &= PATCH_ENSURE(UcasDiffInfo.IsEqual(Other.UcasDiffInfo));
	return bIsEqual;
}
