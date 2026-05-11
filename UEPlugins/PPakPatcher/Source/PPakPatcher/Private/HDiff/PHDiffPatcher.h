#pragma once
#include "CoreMinimal.h"
#include "CoreUObject.h"
#include "CoreTypes.h"
#include "UObject/NameTypes.h"
#include "Engine/EngineTypes.h"
#include "IPBinPatcher.h"

class FPHDiffPatcher : public IPBinPatcher
{
public:
	FPHDiffPatcher();
	virtual ~FPHDiffPatcher();

	bool CreateDiff(const TArray<uint8>& InNew, const TArray<uint8>& InOld, TArray<uint8>& OutDiff,
		EPakPatchCompressType InCompressType = EPakPatchCompressType::None) override;

	bool CreateDiff(uint8* InNew, uint64 InNewSize, uint8* InOld, uint64 InOldSize, TArray<uint8>& OutDiff,
		EPakPatchCompressType InCompressType = EPakPatchCompressType::None) override;

	bool CheckDiff(const TArray<uint8>& InNew, const TArray<uint8>& InOld, const TArray<uint8>& InDiff) override;

	bool CheckDiff(uint8* InNew, uint64 InNewSize, uint8* InOld, uint64 InOldSize, uint8* InDiff, uint64 InDiffSize) override;

	bool Patch(TArray<uint8>& OutNew, const TArray<uint8>& InOld, const TArray<uint8>& InDiff) override;

	bool Patch(uint8* InNew, uint64 InNewSize, uint8* InOld, uint64 InOldSize, uint8* InDiff, uint64 InDiffSize) override;
};