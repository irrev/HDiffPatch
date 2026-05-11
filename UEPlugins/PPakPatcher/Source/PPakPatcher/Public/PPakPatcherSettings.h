#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "PPakPatcherSettings.generated.h"


UCLASS(config=PPakPatcher, defaultconfig)
class PPAKPATCHER_API UPPakPatcherSettings : public UObject
{
	GENERATED_UCLASS_BODY()
public:
	UPPakPatcherSettings(){}
	static UPPakPatcherSettings& Get();

	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	FString NewSignExtension = TEXT(".newsig");

	// pre-cache patch data to memory when loading.
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bPrecachePatchDataOnLoad = false;

	// pre-cache patch data to memory when saving.
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bPrecachePatchDataOnSave = false;

	// double check pak entry post load.
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bDoubleCheckEntry = true;

	// if record index block with binary patch.
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bBinaryPatchIndexBlock = true;

	// if record path block with binary patch.
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bBinaryPatchPathBlock = true;

	// if record head block with binary patch.
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bBinaryPatchHeadBlock = true;

	// 当前运行时无法获取RSA的私密，导致无法正确写出来sig.
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bUseSignWriter = false;

	// serialize .sign file to patch file.
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bRecordSignToPatch = true;

	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bGenPakFileMD5 = false;
};

