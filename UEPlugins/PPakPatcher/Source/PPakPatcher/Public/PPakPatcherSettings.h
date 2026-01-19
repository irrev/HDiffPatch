#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"


class PPAKPATCHER_API FPPakPatcherSettings
{
public:
	FPPakPatcherSettings(){}
	static FPPakPatcherSettings& Get();
	void Load();

	FString NewSignExtension = TEXT(".newsig");

	// pre-cache patch data to memory when loading.
	bool bPrecachePatchDataOnLoad = false;

	// pre-cache patch data to memory when saving.
	bool bPrecachePatchDataOnSave = false;

	// double check pak entry post load.
	bool bDoubleCheckEntry = true;

	// if record index block with binary patch.
	bool bBinaryPatchIndexBlock = true;

	// if record path block with binary patch.
	bool bBinaryPatchPathBlock = true;

	// if record head block with binary patch.
	bool bBinaryPatchHeadBlock = true;

	// 当前运行时无法获取RSA的私密，导致无法正确写出来sig.
	bool bUseSignWriter = false;

	// serialize .sign file to patch file.
	bool bRecordSignToPatch = true;

	bool bGenPakFileMD5 = false;
private:
	bool bHasLoaded = false;
};

