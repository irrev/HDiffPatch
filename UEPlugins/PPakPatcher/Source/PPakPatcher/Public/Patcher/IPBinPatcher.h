#pragma once

#include "CoreMinimal.h"
#include "Data/PPakFileData.h"
#include "Data/PPakPatcherDataType.h"

/**
 * IPBinPatcher : 直接调用HDiffPatch生成diff数据，输入输出都是二进制数据。
 */

class PPAKPATCHER_API IPBinPatcher
{
public:
	virtual ~IPBinPatcher() = default;

	/** 从 UPPakPatcherSettings 重新拉取参数（HDiff 多线程/StepMemSize/MinMatchScore 等）。
	 *  当 Settings 在运行期被修改（例如 Commandlet 命令行覆盖）时，调用此函数让 BinPatcher 实例同步最新值。 */
	virtual void ReloadSettingsFromConfig() {}

	virtual bool CreateDiff(const TArray<uint8>& InNew, const TArray<uint8>& InOld, TArray<uint8>& OutDiff,
		EPakPatchCompressType InCompressType = EPakPatchCompressType::None) = 0;

	virtual bool CreateDiff(uint8* InNew, uint64 InNewSize, uint8* InOld, uint64 InOldSize, TArray<uint8>& OutDiff,
		EPakPatchCompressType InCompressType = EPakPatchCompressType::None) = 0;

	virtual bool CheckDiff(const TArray<uint8>& InNew, const TArray<uint8>& InOld, const TArray<uint8>& InDiff) = 0;

	virtual bool CheckDiff(uint8* InNew, uint64 InNewSize, uint8* InOld, uint64 InOldSize, uint8* InDiff, uint64 InDiffSize) = 0;

	virtual bool Patch(TArray<uint8>& OutNew, const TArray<uint8>& InOld, const TArray<uint8>& InDiff) = 0;

	virtual bool Patch(uint8* InNew, uint64 InNewSize, uint8* InOld, uint64 InOldSize, uint8* InDiff, uint64 InDiffSize) = 0;
};
