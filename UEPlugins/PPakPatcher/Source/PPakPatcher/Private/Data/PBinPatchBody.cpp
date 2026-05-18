#include "Data/PBinPatchBody.h"

#define PATCH_ENSURE ensure

void FPBinPatchBody::Serialize(FArchive& Ar)
{
	DiffInfo.Serialize(Ar);
}

bool FPBinPatchBody::IsEqual(FPBinPatchBody& Other)
{
	bool bIsEqual = true;
	bIsEqual &= PATCH_ENSURE(DiffInfo.IsEqual(Other.DiffInfo));
	return bIsEqual;
}
