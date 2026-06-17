#pragma once

#include "CoreMinimal.h"
#include "Data/PPakPatchBody.h"

/**
 * Bin Body —— 任意二进制文件的整体 diff 载荷。
 *   DiffInfo 指向主文件（.pak 或任意 bin）的 HDiff 字节流。
 *   UtocDiffInfo / UcasDiffInfo 可选，用于 Binary 模式下同名 IoStore 同伴联动。
 */
class PPAKPATCHER_API FPBinPatchBody
{
public:
	/** 主文件 HDiff 字节块描述（Data.bIsPatchData 恒为 true） */
	FPPakPatchDataInfo DiffInfo;

	/** IoStore 同伴：.utoc diff（可选） */
	bool bHasUtocDiff = false;
	FPPakPatchDataInfo UtocDiffInfo;
	int64 UtocOldSize = 0;
	int64 UtocNewSize = 0;

	/** IoStore 同伴：.ucas diff（可选） */
	bool bHasUcasDiff = false;
	FPPakPatchDataInfo UcasDiffInfo;
	int64 UcasOldSize = 0;
	int64 UcasNewSize = 0;

	void Serialize(FArchive& Ar);
	bool IsEqual(FPBinPatchBody& Other);
};
