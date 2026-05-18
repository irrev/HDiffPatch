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

	/** 便捷接口：内部自管 buffer，文件不存在或失败时返回空串 / 0。 */
	static FString CalculateFileMD5String(const FString& InFilename);
	static uint32  CalculateFileCrc32(const FString& InFilename);

	/**
	 * 按 UPPakPatcherSettings::CheckFileHashType 校验文件 hash。
	 *   - None  : 直接 true（不校验）
	 *   - Crc32 : 仅当 ExpectedCrc32 非 0 时校验 CRC32
	 *   - MD5   : 仅当 ExpectedMD5 非空时校验 MD5
	 * 返回 true 表示通过（含"无可比较的期望值因此跳过"的情况）。
	 */
	static bool VerifyFileHashByCheckType(const FString& InFilename,
		const FString& ExpectedMD5, uint32 ExpectedCrc32,
		const TCHAR* InContextTagForLog = nullptr);

	static FPFileCompareInfo CompareFile(const FString& InNewFile, const FString& InOldFile, TArray<uint8>& Buffer, bool bCompareMD5 = false, bool bFastCompare = true);
	static TArray<FPFileCompareInfo> CompareDirectories(const FString& InNewDir, const FString& InOldDir, bool bFastCompare= true);
};