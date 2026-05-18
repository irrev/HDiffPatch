#pragma once

#include "CoreMinimal.h"
#include "Data/PPakPatchBody.h"

/**
 * Bin Body —— 任意二进制文件的单段 diff 载荷。
 *   DiffInfo 指向 FPResPatchData::Data 里的 HDiff 字节流。
 */
class PPAKPATCHER_API FPBinPatchBody
{
public:
	/** HDiff 字节块描述（Data.bIsPatchData 恒为 true） */
	FPPakPatchDataInfo DiffInfo;

	void Serialize(FArchive& Ar);
	bool IsEqual(FPBinPatchBody& Other);
};
