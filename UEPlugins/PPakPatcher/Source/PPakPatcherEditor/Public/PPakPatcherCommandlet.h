#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "PPakPatcherCommandlet.generated.h"

/*
* 功能: 
*	CreatePakPatch: 生成新旧两个pak之间的patch文件。patch文件的后缀为'.patch'
*	参数:
*		NewPak: 新的pak文件路径
*		OldPak: 旧的pak文件路径
*		Patch:	将要生成的patch文件路径
*	例子：
*		UE4Editor-cmd.exe -run="PPakPatcher" -CreatePakPatch -NewPak="" -OldPak="" -Patch=""
* 
* 功能:
*	CheckPakPatch: 检测新旧两个pak和patch文件之间的有效性。
*	参数:
*		NewPak: 新的pak文件路径
*		OldPak: 旧的pak文件路径
*		Patch:	生成的patch文件路径
*	例子：
*		UE4Editor-cmd.exe -run="PPakPatcher" -CheckPakPatch -NewPak="" -OldPak="" -Patch=""
*
* * 功能:
*	PatchPak: 传入旧pak和patch文件，生成出新的pak文件。
*	参数:
*		NewPak: 将要生成的新的pak文件路径
*		OldPak: 旧的pak文件路径
*		Patch:	patch文件路径
*	例子:
*		UE4Editor-cmd.exe -run="PPakPatcher" -PatchPak -NewPak="" -OldPak="" -Patch=""
*
*	功能:
*	CreatePakPatchWithDir: 传入新旧pak路径，根据chunk名称匹配，各自生成'.patch'
*	参数:
*		NewPakDir: 将要生成的新的pak文件路径
*		OldPakDir: 旧的pak文件路径
*		PatchDir:  输出patch文件路径
*		CopyNewIfNoOld:如果找不到匹配的旧的pak，就把新的复制过去。
*	例子:
*		UE4Editor-cmd.exe -run="PPakPatcher" -CreatePakPatchWithDir -NewDir="" -OldDir="" -PatchDir="" -CopyNewIfNoOld
*/


UCLASS()
class UPPakPatcherCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	/** Runs the commandlet */
	virtual int32 Main(const FString& Params) override;

private:
	bool CheckFileParams(const TCHAR* Params, const TCHAR* Match, FString& OutParamValue, bool bCheckExist = false);
	bool CheckDirParams(const TCHAR* Params, const TCHAR* Match, FString& OutParamValue, bool bCheckExist = false, bool bCreateIfNotExist = false);
	int32 CreatePakPatch(const FString& Params);
	bool CreatePakPatch_Internal(const FString& InNewPakFilename, const FString& InOldPakFilename,  const FString& InPatchFilename);
	int32 CheckPakPatch(const FString& Params);
	int32 PatchPak(const FString& Params);

	TArray<FString> GatherPaksInDirectory(const FString InDir);
	TMap<FString, FString> MakeNewOldMatchMap(const FString& InNewDir, const FString& InOldDir);
	int32 CreatePakPatchWithDir(const FString& Params);
};