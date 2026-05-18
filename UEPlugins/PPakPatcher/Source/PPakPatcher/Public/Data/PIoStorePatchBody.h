#pragma once

#include "CoreMinimal.h"
#include "Data/PPakPatchBody.h"

/**
 * IoStore diff 策略。
 */
enum class EIoStoreDiffStrategy : uint8
{
	/** 文件级 binary diff：把 .utoc / .ucas 作为不透明字节流各自做 HDiff。 */
	FileBinary = 0,
	/** 未来扩展：按 chunk / 压缩块粒度做结构化 diff。 */
	ChunkAware = 1,
};

/**
 * IoStore Body —— .utoc + .ucas 文件对的补丁载荷。
 *   UtocDiffInfo / UcasDiffInfo 分别指向 FPResPatchData::Data 里两段 HDiff。
 *
 * 文件 hash 双轨（MD5 + Crc32）：构建侧同时写两者，运行时按 CheckFileHashType 选择校验。
 */
class PPAKPATCHER_API FPIoStorePatchBody
{
public:
	EIoStoreDiffStrategy Strategy = EIoStoreDiffStrategy::FileBinary;

	/** .utoc 整体 diff */
	FPPakPatchDataInfo UtocDiffInfo;
	FString OldUtocMD5;
	FString NewUtocMD5;
	uint32 OldUtocCrc32 = 0;
	uint32 NewUtocCrc32 = 0;

	/** .ucas 整体 diff */
	FPPakPatchDataInfo UcasDiffInfo;
	FString OldUcasMD5;
	FString NewUcasMD5;
	uint32 OldUcasCrc32 = 0;
	uint32 NewUcasCrc32 = 0;

	void Serialize(FArchive& Ar);
	bool IsEqual(FPIoStorePatchBody& Other);
};
