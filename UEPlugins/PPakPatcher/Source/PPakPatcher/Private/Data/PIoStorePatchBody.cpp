#include "Data/PIoStorePatchBody.h"

#define PATCH_ENSURE ensure

void FPIoStorePatchBody::Serialize(FArchive& Ar)
{
	uint8 StrategyByte = static_cast<uint8>(Strategy);
	Ar << StrategyByte;
	if (Ar.IsLoading())
	{
		Strategy = static_cast<EIoStoreDiffStrategy>(StrategyByte);
	}

	UtocDiffInfo.Serialize(Ar);
	Ar << OldUtocMD5;
	Ar << NewUtocMD5;
	Ar << OldUtocCrc32;
	Ar << NewUtocCrc32;

	UcasDiffInfo.Serialize(Ar);
	Ar << OldUcasMD5;
	Ar << NewUcasMD5;
	Ar << OldUcasCrc32;
	Ar << NewUcasCrc32;
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
	bIsEqual &= PATCH_ENSURE(UcasDiffInfo.IsEqual(Other.UcasDiffInfo));
	bIsEqual &= PATCH_ENSURE(OldUcasMD5 == Other.OldUcasMD5);
	bIsEqual &= PATCH_ENSURE(NewUcasMD5 == Other.NewUcasMD5);
	bIsEqual &= PATCH_ENSURE(OldUcasCrc32 == Other.OldUcasCrc32);
	bIsEqual &= PATCH_ENSURE(NewUcasCrc32 == Other.NewUcasCrc32);
	return bIsEqual;
}
