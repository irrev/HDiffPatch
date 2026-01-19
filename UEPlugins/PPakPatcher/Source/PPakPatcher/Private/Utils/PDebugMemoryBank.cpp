#include "Utils/PDebugMemoryBank.h"
#include "Misc/LazySingleton.h"

FPDebugMemoryBank& FPDebugMemoryBank::Get()
{
	return TLazySingleton<FPDebugMemoryBank>::Get();
}

void FPDebugMemoryBank::CacheMemory(const FName& InName, uint8* InData, int64 InSize)
{
	TArray<uint8>& Data = Datas.FindOrAdd(InName);
	Data.Empty();
	Data.SetNum(InSize);
	FMemory::Memcpy(Data.GetData(), InData, InSize);
}

TArray<uint8>* FPDebugMemoryBank::GetCachedMemory(const FName& InName)
{
	return Datas.Find(InName);
}

void FPDebugMemoryBank::RemoveCache(const FName& InName)
{
	Datas.Remove(InName);
}

