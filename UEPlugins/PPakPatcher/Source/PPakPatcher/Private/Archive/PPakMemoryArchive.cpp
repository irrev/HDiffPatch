#include "Archive/PPakMemoryArchive.h"
#include "Data/PPakFileData.h"
#include "Data/PPakPatcherDataType.h"

#include "Logging/LogMacros.h"
#include "CoreGlobals.h"

/*----------------------------------------------------------------------------
	FPPakMemoryArchive
----------------------------------------------------------------------------*/

FPPakMemoryArchive::FPPakMemoryArchive(const int64 PreAllocateBytes, bool bIsPersistent, const TCHAR* InFilename)
	: FMemoryArchive()
	, Data(PreAllocateBytes)
	, ArchiveName(InFilename ? InFilename : TEXT("FPPakMemoryArchive"))
{
	this->SetIsSaving(true);
	this->SetIsPersistent(bIsPersistent);
}

void FPPakMemoryArchive::Serialize(void* InData, int64 Num)
{
	UE_CLOG(!Data.HasData(), LogPPakPacher, Fatal, TEXT("Tried to serialize data to an FPPakMemoryArchive that was already released. Archive name: %s."), *ArchiveName);

	if (Data.Write(InData, Offset, Num))
	{
		Offset += Num;
	}
}

FString FPPakMemoryArchive::GetArchiveName() const
{
	return ArchiveName;
}

uint8* FPPakMemoryArchive::GetData() const
{
	UE_CLOG(!Data.HasData(), LogPPakPacher, Warning, TEXT("Tried to get written data from an FPPakMemoryArchive that was already released. Archive name: %s."), *ArchiveName);
	return const_cast<uint8*>(Data.GetData());
}

void FPPakMemoryArchive::Resize(int64 InNewSize)
{
	Data.Resize(InNewSize);
}

void FPPakMemoryArchive::Reserve(int64 NewMax)
{
	Data.Reserve(NewMax);
}

bool FPPakMemoryArchive::operator==(const FPPakMemoryArchive& Other)
{
	if (GetSize() == Other.GetSize())
	{
		return FMemory::Memcmp(GetData(), Other.GetData(), GetSize()) == 0;
	}
	return false;
}

bool FPPakMemoryArchive::operator!=(const FPPakMemoryArchive& Other)
{
	return !operator==(Other);
}
