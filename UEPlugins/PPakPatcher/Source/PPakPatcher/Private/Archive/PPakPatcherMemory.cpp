#include "Archive/PPakPatcherMemory.h"
#include "Logging/LogMacros.h"
#include "CoreGlobals.h"

/*----------------------------------------------------------------------------
	FPPakPatcherMemory
----------------------------------------------------------------------------*/

FPPakPatcherMemory::FPPakPatcherMemory(const int64 PreAllocateBytes)
	: Data(nullptr)
	, NumBytes(FMath::Max<int64>(0, PreAllocateBytes))
	, MaxBytes(0)
{
	GrowBuffer();
}

FPPakPatcherMemory::~FPPakPatcherMemory()
{
	if (Data)
	{
		FMemory::Free(Data);
	}
}

bool FPPakPatcherMemory::Write(void* InData, int64 InOffset, int64 InNum)
{
	// Allow InData to be null if InNum == 0.
	if (InOffset >= 0 && (InData ? InNum >= 0 : InNum == 0))
	{
		// Grow the buffer to the offset position even if InNum == 0.
		NumBytes = FMath::Max<int64>(NumBytes, InOffset + InNum);
		if (NumBytes > MaxBytes)
		{
			GrowBuffer();
		}

		if (InNum)
		{
			FMemory::Memcpy(&Data[InOffset], InData, InNum);
		}

		return true;
	}
	else
	{
		return false;
	}
}

bool FPPakPatcherMemory::Read(void* OutData, int64 InOffset, int64 InNum) const
{
	// Allow OutData to be null if InNum == 0.
	if (InOffset >= 0 && (OutData ? InNum >= 0 : InNum == 0) && (InOffset + InNum <= NumBytes))
	{
		if (InNum)
		{
			FMemory::Memcpy(OutData, &Data[InOffset], InNum);
		}

		return true;
	}
	else
	{
		return false;
	}
}

uint8* FPPakPatcherMemory::ReleaseOwnership()
{
	uint8* ReturnData = Data;

	Data = nullptr;
	NumBytes = 0;
	MaxBytes = 0;

	return ReturnData;
}

void FPPakPatcherMemory::Resize(int64 Size)
{
	NumBytes = FMath::Max<int64>(NumBytes, Size);
	if (NumBytes > MaxBytes)
	{
		GrowBuffer();
	}
}

void FPPakPatcherMemory::Reserve(int64 NewMax)
{
	if (MaxBytes < NewMax)
	{
		// Allocate slack proportional to the buffer size. Min 64 KB
		MaxBytes = NewMax;

		Data = (uint8*)FMemory::Realloc(Data, MaxBytes);
	}
}

void FPPakPatcherMemory::GrowBuffer()
{
	// Allocate slack proportional to the buffer size. Min 64 KB
	MaxBytes = FMath::Max<int64>(64 * 1024, FMemory::QuantizeSize(NumBytes + 3 * NumBytes / 8 + 16));

	if (Data)
	{
		Data = (uint8*)FMemory::Realloc(Data, MaxBytes);
	}
	else
	{
		Data = (uint8*)FMemory::Malloc(MaxBytes);
	}
}
