#pragma once

#include "CoreMinimal.h"
#include "IPlatformFilePak.h"
#include "Data/PPakPatcherDataType.h"

// -----------------------------------------------------------------------------
// 通用补丁数据块描述（Bin / Pak / IoStore 共用）
// -----------------------------------------------------------------------------

class PPAKPATCHER_API FPPakPatchDataInfo
{
public:
	bool bIsPatchData = false; // 若为 true 表示 DataBlock 里的字节是 HDiff 差量；否则为 full data
	int64 NewOffset = 0;
	int64 NewSize = 0;
	int64 OldOffset = 0;
	int64 OldSize = 0;
	int64 DataOffset = 0;       // 在 FPResPatchData::Data 池 / 磁盘 DataBlock 内的偏移
	int64 DataSize = 0;         // 该条记录原始（解压后）字节数；HDiff Patch 时需要这个长度
	int64 CompressedSize = 0;   // 该条记录在 Data 池 / 磁盘上实际占用的字节数。
	                            //   == DataSize 表示该条未压缩（直接 memcpy）；
	                            //   < DataSize 表示已被 Oodle 压缩（GetFilePatchData 时按需解压）。
	                            //   v8 起每条记录独立压缩（不再使用整 DataBlock 切块压缩）。

	void Serialize(FArchive& Ar);
	bool IsEqual(FPPakPatchDataInfo& Other);
};

// -----------------------------------------------------------------------------
// v6: 单个 CompressionBlock 的 patch 描述
// 当 NewEntry/OldEntry 的 CompressionBlocks 数量一致时，可按 block 粒度做 diff/patch，
// 大幅降低 PatchPak 运行时单 entry 的工作集（≈ 1 block）。
// -----------------------------------------------------------------------------

class PPAKPATCHER_API FPPakBlockPatchInfo
{
public:
	FPPakPatchDataInfo BlockPatchData;

	void Serialize(FArchive& Ar);
	bool IsEqual(FPPakBlockPatchInfo& Other);
};

// -----------------------------------------------------------------------------
// Pak 单文件 entry 的 patch 描述
// -----------------------------------------------------------------------------

class PPAKPATCHER_API FPPakFilePatchInfo
{
public:
	FString FileName;
	int64 FileUncompressedSize = 0;
	int64 FileRealSize = 0;       // EntrySize + FileSize
	int64 OldFileRealSize = 0;
	EPakFilePatchType PatchType = EPakFilePatchType::Keep;
	FPPakPatchDataInfo DataInfo;

	FPakEntry Entry;

	/**
	 * v6: per-block diff 数组。非空表示按 CompressionBlock 粒度 diff/patch，
	 * 此时 DataInfo.bIsPatchData/DataSize 不被使用；空数组表示走整 entry HDiff（DataInfo 有效）。
	 * 仅 PakAware DAC 模式 + Modify 类型 + New/Old block 数一致时启用。
	 */
	TArray<FPPakBlockPatchInfo> BlockPatches;

	void Serialize(FArchive& Ar);
	bool IsEqual(FPPakFilePatchInfo& Other);
};

// -----------------------------------------------------------------------------
// Pak Body —— pak 专属补丁载荷
//   由原 FPPakPatchInfo 去掉 NewPakName / OldPakName / NewPakHash / OldPakHash /
//   NewVersion / OldVersion 字段（这些统一到 FPResPatchHeader 公共头）。
//   其余 pak 专属字段保留。
// -----------------------------------------------------------------------------

class PPAKPATCHER_API FPPakPatchBody
{
public:
	/*
		pak v10+ layout (Primary/PathHash/FullDirectory 三段独立)：
			-> [padding + file entry + data] x n
			-> primary index
			-> path hash index        (optional)
			-> full directory index   (optional)
			-> head (FPakInfo footer)
	*/

	/* file entry + data record array */
	TArray<FPPakFilePatchInfo> FilePatchInfos;

	/* Primary Index 块 patch */
	FPPakPatchDataInfo IndexPatchInfo;

	/* Path Hash Index 块 patch */
	bool bHasPathHashIndex = false;
	FPPakPatchDataInfo PathHashPatchInfo;
	FSHAHash PathHashIndexHash;

	/* Full Directory Index 块 patch */
	bool bHasFullDirectoryIndex = false;
	FPPakPatchDataInfo FullDirectoryPatchInfo;
	FSHAHash FullDirectoryIndexHash;

	/* Head (FPakInfo footer) 块 patch */
	FPPakPatchDataInfo HeadPatchInfo;

	/* Signature file (.sig) */
	FPPakPatchDataInfo SignFileInfo;

	/* Primary Index 的 Hash（来自 FPakInfo.IndexHash） */
	FSHAHash IndexHash;

	bool bSign = false;
	FString MountPoint;

	void Serialize(FArchive& Ar);
	bool IsEqual(FPPakPatchBody& Other);
};
