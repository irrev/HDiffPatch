#pragma once

#include "CoreMinimal.h"
#include "Data/PPakFileData.h"
#include "Data/PPakPatchData.h"
#include "IPPakPatcher.h"

class FPPakPatcher : public IPPakPatcher
{
public:
	FPPakPatcher() {}
	virtual ~FPPakPatcher() {}

	virtual bool CreatePakDiff(const FString& InPatchFilename, const FPPakFileDataPtr& InNewPak, const FPPakFileDataPtr& InOldPak, FPPakPatchDataPtr& OutPatch) override;
	virtual bool PatchPak(const FString& InNewPakFilename, const FPPakFileDataPtr& InOldPak, const FPPakPatchDataPtr& InPatch) override;
	virtual bool CheckPakDiff(const FPPakFileDataPtr& InNewPak, const FPPakFileDataPtr& InOldPak, const FPPakPatchDataPtr& InPatch) override;
};
