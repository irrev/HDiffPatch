#pragma once

#include "CoreMinimal.h"
#include "Data/PPakFileData.h"
#include "Data/PPakPatchData.h"

class PPAKPATCHER_API IPPakPatcher
{
public:
	IPPakPatcher() {}
	virtual ~IPPakPatcher() {}

	virtual bool CreatePakDiff(const FString& InPatchFilename, const FPPakFileDataPtr& InNewPak, const FPPakFileDataPtr& InOldPak, FPPakPatchDataPtr& OutPatch) = 0;
	virtual bool PatchPak(const FString& InNewPakFilename, const FPPakFileDataPtr& InOldPak, const FPPakPatchDataPtr& InPatch) = 0;
	virtual bool CheckPakDiff(const FPPakFileDataPtr& InNewPak, const FPPakFileDataPtr& InOldPak, const FPPakPatchDataPtr& InPatch) = 0;
};
