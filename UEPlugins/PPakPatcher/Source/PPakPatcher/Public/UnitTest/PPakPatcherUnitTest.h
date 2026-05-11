#pragma once
#include "CoreMinimal.h"
#include "PPakPatcherModule.h"

struct FPPakPatcherUnitTestParams
{
	FString NewFile;
	FString OldFile;
	FString NewDir;
	FString OldDir;
	FString OutputDir;
};


class PPAKPATCHER_API FPPakPatcherUnitTest
{
public:
	bool bUseBinaryPatcher = true;
	bool bUsePakPatcher = false;

	static FPPakPatcherUnitTest& Get();
	bool SimpleTest();
	bool DirecotryDiffPatchTest(const FString& InNewDir, const FString& InOldDir, const FString& InOutputDir, const FString& InPatchDir, const FString& InNewPatchedDir);

	bool TestBinaryPatcher(const FString& InNewFile, const FString& InOldFile);
	bool TestPakPatcher(const FString& InNewFile, const FString& InOldFile);

};

