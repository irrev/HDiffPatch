#pragma once
#include "CoreMinimal.h"
#include "Patcher/IPBinPatcher.h"
#include "Patcher/PPakPatcher.h"
#include "Patcher/PIoStorePatcher.h"
#include "Data/PPakPatcherDataType.h"
#include "Data/PResPatchData.h"

/**
 * FPResPatcher : 资源补丁统一入口。根据文件扩展名，分发到不同 Patcher：
 *   1. 非 Pak 资源（非 .pak/.utoc/.ucas）：调用 IPBinPatcher 生成补丁，结果存入 FPResPatchData.BinBody。
 *   2. Pak 资源(.pak)：调用 FPPakPatcher 生成补丁，结果存入 FPResPatchData.PakBody；
 *                       若同目录存在同名 .utoc/.ucas，会由 FPPakPatcher 内部同步 IoStore 补丁（此路径暂待实装）。
 *   3. .utoc/.ucas 不应该被单独传入（调用方应该传 .pak；IoStore 由 pak 同伴机制处理）。
 *
 * 对外仅暴露 3 个接口：CreateDiff / PatchDiff / CheckDiff，
 * 入参只接受文件路径或 FPResPatchDataPtr；FPPakFileData 由下层 Patcher 内部生成并持有。
 */

class PPAKPATCHER_API FPResPatcher
{
public:
	FPResPatcher();
	virtual ~FPResPatcher();

	/** 判断文件是否为 .pak 资源 */
	static bool IsPak(const FString& InFilename);

	/** 判断文件是否为 IoStore 资源（.utoc/.ucas） */
	static bool IsIoStore(const FString& InFilename);

	/**
	 * 生成差量补丁。根据新文件扩展名自动分发。
	 */
	bool CreateDiff(const FString& InPatchFilename,
		const FString& InNewFile, const FString& InOldFile,
		FPResPatchDataPtr& OutPatch,
		EPPakPatchMode InMode = EPPakPatchMode::PakAware,
		EPakPatchCompressType InCompressType = EPakPatchCompressType::None);

	bool PatchDiff(const FString& InNewFile, const FString& InOldFile, const FPResPatchDataPtr& InPatch);

	bool CheckDiff(const FString& InNewFile, const FString& InOldFile, const FPResPatchDataPtr& InPatch);

private:
	/** 按需创建 PakPatcher（懒加载，per-task 状态） */
	FPPakPatcher* GetOrCreatePakPatcher();

	/** 借用 Module 的单实例 BinPatcher（无状态服务，无需懒加载） */
	IPBinPatcher* GetBinPatcher();

	// -----------------------------------------------------------------
	// Bin 分支专用 helper（仅 .pak/.utoc/.ucas 之外的普通文件走这里）
	// -----------------------------------------------------------------

	/** Bin 文件 diff 生成；负责把 HDiff 字节录入 OutPatch.BinBody 并按需写盘。 */
	bool CreateBinDiff(const FString& InPatchFilename,
		const FString& InNewFile, const FString& InOldFile,
		FPResPatchDataPtr& OutPatch,
		EPakPatchCompressType InCompressType);

	/** Bin 文件回放：按 InPatch.BinBody 把 InOldFile 还原为 InNewFile。 */
	bool PatchBin(const FString& InNewFile, const FString& InOldFile, const FPResPatchDataPtr& InPatch);

	/** Bin 文件回测：用 InPatch.BinBody 校验 (InNewFile, InOldFile) 是否一致。 */
	bool CheckBinDiff(const FString& InNewFile, const FString& InOldFile, const FPResPatchDataPtr& InPatch);

	TSharedPtr<FPPakPatcher>  PakPatcher;
};
