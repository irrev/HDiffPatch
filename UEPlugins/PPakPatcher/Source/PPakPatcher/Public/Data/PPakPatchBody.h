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
	int64 DataOffset = 0;   // 在 FPResPatchData::Data 里的偏移
	int64 DataSize = 0;     // DataBlock 里实际承载的字节数（差量字节数 或 full data 字节数）

	void Serialize(FArchive& Ar);
	bool IsEqual(FPPakPatchDataInfo& Other);
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
