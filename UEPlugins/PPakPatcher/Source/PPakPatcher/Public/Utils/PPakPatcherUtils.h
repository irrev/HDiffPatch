#pragma once

#include "CoreMinimal.h"
#include "CoreUObject.h"
#include "CoreTypes.h"
#include "UObject/NameTypes.h"
#include "Engine/EngineTypes.h"
#include "Serialization/MemoryArchive.h"

#include "Data/PPakPatcherDataType.h"

class PPAKPATCHER_API FPPakPatcherUtils

{
public:
	static bool LoadFileToBuffer(const FString& InFileName, TArray<uint8>& OutBuffer);
	static bool LoadFileToBuffer(const FString& InFileName, TArray64<uint8>& OutBuffer);
	static bool DumpMemoryToFile(const FString& InFilename, uint8* InData, int64 InSize);

	static int32 CalculateFileCrc32(const FString& InFilename, TArray<uint8>& Buffer);
	static FMD5Hash CalculateFileMD5(const FString& InFilename, TArray<uint8>& Buffer);
	static int64 GetFileSize(const FString& InFilename);

	static FPFileCompareInfo CompareFile(const FString& InNewFile, const FString& InOldFile, TArray<uint8>& Buffer, bool bCompareMD5 = false, bool bFastCompare = true);
	static TArray<FPFileCompareInfo> CompareDirectories(const FString& InNewDir, const FString& InOldDir, bool bFastCompare= true);
};