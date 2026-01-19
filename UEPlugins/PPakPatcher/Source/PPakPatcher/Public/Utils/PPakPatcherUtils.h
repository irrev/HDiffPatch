#pragma once

#include "CoreMinimal.h"
#include "CoreUObject.h"
#include "CoreTypes.h"
#include "UObject/NameTypes.h"
#include "Engine/EngineTypes.h"
#include "Serialization/MemoryArchive.h"

class FPPakPatcherUtils
{
public:
	static bool LoadFileToBuffer(const FString& InFileName, TArray<uint8>& OutBuffer);
	static bool TestBinaryPatch(const FString& InNewFile, const FString& InOldFile);
	static bool TestPakPatch(const FString& InNewPakFile, const FString& InOldPakFile);

	static bool DumpMemoryToFile(const FString& InFilename, uint8* InData, int64 InSize);
};