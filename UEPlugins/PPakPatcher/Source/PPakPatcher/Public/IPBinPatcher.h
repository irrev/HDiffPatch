#pragma once

#include "CoreMinimal.h"
#include "Data/PPakFileData.h"
#include "Data/PPakPatcherDataType.h"

class PPAKPATCHER_API IPBinPatcher
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

	virtual bool CreateDiff(const TArray<uint8>& InNew, const TArray<uint8>& InOld, TArray<uint8>& OutDiff,
		EPakPatchCompressType InCompressType = EPakPatchCompressType::None) = 0;

	virtual bool CreateDiff(uint8* InNew, uint64 InNewSize, uint8* InOld, uint64 InOldSize, TArray<uint8>& OutDiff,
		EPakPatchCompressType InCompressType = EPakPatchCompressType::None) = 0;

	virtual bool CheckDiff(const TArray<uint8>& InNew, const TArray<uint8>& InOld, const TArray<uint8>& InDiff) = 0;

	virtual bool CheckDiff(uint8* InNew, uint64 InNewSize, uint8* InOld, uint64 InOldSize, uint8* InDiff, uint64 InDiffSize) = 0;

	virtual bool Patch(TArray<uint8>& OutNew, const TArray<uint8>& InOld, const TArray<uint8>& InDiff) = 0;

	virtual bool Patch(uint8* InNew, uint64 InNewSize, uint8* InOld, uint64 InOldSize, uint8* InDiff, uint64 InDiffSize) = 0;
};
