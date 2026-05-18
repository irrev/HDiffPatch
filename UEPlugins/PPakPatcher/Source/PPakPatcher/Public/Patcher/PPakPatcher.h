#pragma once

#include "CoreMinimal.h"
#include "Data/PPakFileData.h"
#include "Data/PResPatchData.h"
#include "Data/PPakPatcherDataType.h"

/*
 * FPPakPatcher : .pak 资源补丁器。入参只能是 .pak。
 * 处理完 .pak 如果存在同名的 .ucas/.utoc，会自动调用 FPIoStorePatcher 把 IoStore
 * 部分也填入同一个 FPResPatchData（供 FPResPatcher 统一对外）。
 *
 * 对外提供两组接口：
 *   1. 文件路径接口：CreateDiff / PatchDiff / CheckDiff
 *      内部自行加载 FPPakFileData 并作为成员变量持有。
 *   2. FPPakFileDataPtr 接口：CreatePakDiff / PatchPak / CheckPakDiff
 *      供已持有 FPPakFileData 的底层调用方（Commandlet / UnitTest）复用。
 */

class PPAKPATCHER_API FPPakPatcher
{
public:
	FPPakPatcher() {}
	virtual ~FPPakPatcher() {}

	// -----------------------------------------------------------------------
	// 文件路径接口（FPPakFileData 由本类内部生成并持有）
	// -----------------------------------------------------------------------

	bool CreateDiff(const FString& InPatchFilename, const FString& InNewPakFile, const FString& InOldPakFile, FPResPatchDataPtr& OutPatch,
		EPPakPatchMode InMode = EPPakPatchMode::PakAware,
		EPakPatchCompressType InCompressType = EPakPatchCompressType::None);
	bool PatchDiff(const FString& InNewPakFilename, const FString& InOldPakFile, const FPResPatchDataPtr& InPatch);
	bool CheckDiff(const FString& InNewPakFile, const FString& InOldPakFile, const FPResPatchDataPtr& InPatch);

	// -----------------------------------------------------------------------
	// FPPakFileDataPtr 接口（底层）
	// -----------------------------------------------------------------------

	bool CreatePakDiff(const FString& InPatchFilename, const FPPakFileDataPtr& InNewPak, const FPPakFileDataPtr& InOldPak, FPResPatchDataPtr& OutPatch,
		EPPakPatchMode InMode = EPPakPatchMode::PakAware,
		EPakPatchCompressType InCompressType = EPakPatchCompressType::None);
	bool PatchPak(const FString& InNewPakFilename, const FPPakFileDataPtr& InOldPak, const FPResPatchDataPtr& InPatch);
	bool CheckPakDiff(const FPPakFileDataPtr& InNewPak, const FPPakFileDataPtr& InOldPak, const FPResPatchDataPtr& InPatch);

private:
	/** 按需加载并缓存 FPPakFileData（按文件路径去重） */
	FPPakFileDataPtr LoadOrGetPakData(const FString& InPakFilename);

	/** 内部持有的 Pak 数据（以标准化文件路径为 key 缓存） */
	TMap<FString, FPPakFileDataPtr> CachedPakData;
};
