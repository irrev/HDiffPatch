#pragma once

#include "CoreMinimal.h"
#include "Data/PPakPatchBody.h"
#include "IO/IoChunkId.h"

/**
 * IoStore diff 策略。
 */
enum class EIoStoreDiffStrategy : uint8
{
	/** 文件级 binary diff：把 .utoc / .ucas 作为不透明字节流各自做 HDiff。 */
	FileBinary = 0,
	/** Chunk 粒度 diff：解析 .utoc 获取 entry 列表，逐 chunk 从 .ucas 读取数据做 HDiff。 */
	ChunkAware = 1,
};

/**
 * IoStore 单 chunk 的 patch 描述（ChunkAware 策略使用）。
 */
enum class EIoStoreChunkPatchType : uint8
{
	Keep   = 0,  // 新旧数据完全一致，运行时直接拷贝
	Modify = 1,  // 数据有差异，需要 HDiff patch
	New    = 2,  // 新增 chunk（Old 中不存在），full data
	Delete = 3,  // 删除 chunk（New 中不存在），运行时忽略
};

class PPAKPATCHER_API FPIoStoreChunkPatchInfo
{
public:
	FIoChunkId ChunkId;
	EIoStoreChunkPatchType PatchType = EIoStoreChunkPatchType::Keep;

	int64 OldOffset = 0;
	int64 OldLength = 0;
	int64 NewOffset = 0;
	int64 NewLength = 0;

	/** diff / full data 块描述 */
	FPPakPatchDataInfo DataInfo;

	void Serialize(FArchive& Ar);
};

/**
 * IoStore Body —— .utoc + .ucas 文件对的补丁载荷。
 *
 * FileBinary 模式：
 *   UtocDiffInfo / UcasDiffInfo 分别指向整体 HDiff 字节流。
 *
 * ChunkAware 模式：
 *   UtocDiffInfo 仍为 .utoc 整体 diff（utoc 是纯元数据，体积小）；
 *   ChunkPatchInfos 包含每个 chunk 的 diff 描述，.ucas 数据通过 chunk 逐一 diff。
 *
 * 文件 hash 双轨（MD5 + Crc32）：构建侧同时写两者，运行时按 CheckFileHashType 选择校验。
 */
class PPAKPATCHER_API FPIoStorePatchBody
{
public:
	EIoStoreDiffStrategy Strategy = EIoStoreDiffStrategy::FileBinary;

	/** .utoc 整体 diff（FileBinary 和 ChunkAware 均使用） */
	FPPakPatchDataInfo UtocDiffInfo;
	FString OldUtocMD5;
	FString NewUtocMD5;
	uint32 OldUtocCrc32 = 0;
	uint32 NewUtocCrc32 = 0;

	/** .ucas 整体 diff（仅 FileBinary 使用） */
	FPPakPatchDataInfo UcasDiffInfo;
	FString OldUcasMD5;
	FString NewUcasMD5;
	uint32 OldUcasCrc32 = 0;
	uint32 NewUcasCrc32 = 0;

	/** .ucas 文件大小（ChunkAware 用于分配 buffer；含尾部 padding） */
	int64 OldUcasFileSize = 0;
	int64 NewUcasFileSize = 0;

	/** Per-chunk patch info（仅 ChunkAware 使用） */
	TArray<FPIoStoreChunkPatchInfo> ChunkPatchInfos;

	void Serialize(FArchive& Ar);
	bool IsEqual(FPIoStorePatchBody& Other);
};
