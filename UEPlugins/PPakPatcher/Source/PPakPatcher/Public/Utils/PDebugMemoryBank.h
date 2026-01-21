#pragma once
#include "CoreMinimal.h"
#include "Data/PPakFileData.h"

class PPAKPATCHER_API FPDebugMemoryBank
{
public:
	FPDebugMemoryBank(){}
	static FPDebugMemoryBank& Get();

	void CacheMemory(const FName& InName, uint8* InData, int64 InSize);
	TArray<uint8>* GetCachedMemory(const FName& InName);
	void RemoveCache(const FName& InName);

private:
	TMap<FName, TArray<uint8>> Datas;
};