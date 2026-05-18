#pragma once
#include "CoreMinimal.h"
#include "CoreUObject.h"
#include "CoreTypes.h"
#include "UObject/NameTypes.h"
#include "Engine/EngineTypes.h"
#include "Patcher/IPBinPatcher.h"

class FPHDiffPatcher : public IPBinPatcher
{
public:

	bool bUseSingleCompressMode = true;

	// default 6, bin: 0--4  text: 4--9
	int32 MinSingleMatchScore = 6;

	// big cache max used O(oldSize) memory, match speed faster, but build big cache slow 
	bool bUseBigCacheMatch = false;

	int32 ThreadNum = 1;

	//patchStepMemSize: default 256k, recommended 64k,2m etc...
	int32 PatchStepMemSize = 262144;// 1024 * 256;

	bool bSettingsLoaded = false;

	FPHDiffPatcher();
	virtual ~FPHDiffPatcher();

	void ApplySettingsFromConfig();

	virtual void ReloadSettingsFromConfig() override { ApplySettingsFromConfig(); }

	/** 确保 Settings 已从 Config 加载过（懒加载，首次调用时才读 CDO）。 */
	void EnsureSettingsLoaded()
	{
		if (!bSettingsLoaded)
		{
			ApplySettingsFromConfig();
		}
	}

	bool CreateDiff(const TArray<uint8>& InNew, const TArray<uint8>& InOld, TArray<uint8>& OutDiff,
		EPakPatchCompressType InCompressType = EPakPatchCompressType::None) override;

	bool CreateDiff(uint8* InNew, uint64 InNewSize, uint8* InOld, uint64 InOldSize, TArray<uint8>& OutDiff,
		EPakPatchCompressType InCompressType = EPakPatchCompressType::None) override;

	bool CheckDiff(const TArray<uint8>& InNew, const TArray<uint8>& InOld, const TArray<uint8>& InDiff) override;

	bool CheckDiff(uint8* InNew, uint64 InNewSize, uint8* InOld, uint64 InOldSize, uint8* InDiff, uint64 InDiffSize) override;

	bool Patch(TArray<uint8>& OutNew, const TArray<uint8>& InOld, const TArray<uint8>& InDiff) override;

	bool Patch(uint8* InNew, uint64 InNewSize, uint8* InOld, uint64 InOldSize, uint8* InDiff, uint64 InDiffSize) override;
};