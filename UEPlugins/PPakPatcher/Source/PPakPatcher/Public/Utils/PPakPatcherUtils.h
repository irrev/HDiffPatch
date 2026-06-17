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
	 * 单 pass 同时计算 MD5 + CRC32 + FileSize（性能优化，避免 3 次全文件 IO）。
	 * 修复 #10：CreatePatch 主循环中每个 chunk 的 .pak/.utoc/.ucas 都需要 MD5/CRC32/Size
	 * 三个值，旧实现各调一个独立 helper → 同一文件被读取 3 次。改用本接口后只读 1 次。
	 *
	 * @param InFilename 文件路径
	 * @param OutMD5     输出：MD5 字符串（hex 形式，与 CalculateFileMD5String 兼容）；失败时为空
	 * @param OutCRC32   输出：CRC32（与 CalculateFileCrc32 一致）；失败时为 0
	 * @param OutSize    输出：文件字节数；失败时为 0
	 * @return true 表示文件已存在且全部读取成功
	 */
	static bool CalculateFileHashesAndSize(const FString& InFilename,
		FString& OutMD5, uint32& OutCRC32, int64& OutSize);

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