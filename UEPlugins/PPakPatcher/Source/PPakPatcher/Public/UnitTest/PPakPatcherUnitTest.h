#pragma once
#include "CoreMinimal.h"
#include "PPakPatcherModule.h"

// 已弃用：FPPakPatcherUnitTest 走旧 API（FPPakPatcher::CreatePakDiff），与现主流程
// （FPPatchManager / FPResPatcher / TaskRunner）脱节。新代码请使用 commandlet 或 FPPatchManager。
// 端到端 / 多 chunk / 三线程模式覆盖的新单测计划见 TODO_LIST.md #17。

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

