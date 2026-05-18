#pragma once

#include "CoreMinimal.h"
#include "Data/PResPatchData.h"
#include "Data/PPakPatcherDataType.h"

/**
 * FPIoStorePatcher : 处理与 .pak 同名同目录的 .utoc + .ucas（IoStore 容器）。
 *
 * 当前策略：FileBinary —— .utoc 与 .ucas 各自当作不透明字节流走 HDiff。
 * Patch 结果写入既有 FPResPatchData 的 IoStoreBody（新建一个 IoStoreBody，
 * 由调用方决定如何组合到 PakBody / 单独使用）。
 *
 * 三个核心接口都直接吃 .pak 文件路径，函数内部派生出对应的 .utoc / .ucas 路径。
 */

class FPIoStorePatcher
{
public:
	FPIoStorePatcher() {}
	virtual ~FPIoStorePatcher() {}

	/**
	 * 探测同名 .utoc / .ucas 是否存在（任一存在即返回 true）。
	 */
	static bool HasIoStoreSibling(const FString& InPakFile);

	/**
	 * 给 InOutPatch（已经是 Pak/IoStore 类型）追加/填充 IoStore body。
	 *   - 若 IoStoreBody 已存在则直接写入；不存在则懒加载分配。
	 *   - InOutPatch 的 Header.Type 不会被修改（外层 Pak 流程仍可保持 Pak）。
	 */
	bool CreateIoStoreDiff(const FString& InPakNewFile, const FString& InPakOldFile, FPResPatchData& InOutPatch);

	/** 应用 IoStore 部分到磁盘（产出 New .utoc/.ucas） */
	bool PatchIoStore(const FString& InPakNewFile, const FString& InPakOldFile, const FPResPatchData& InPatch);

	/** 校验 IoStore 部分（回测） */
	bool CheckIoStoreDiff(const FString& InPakNewFile, const FString& InPakOldFile, const FPResPatchData& InPatch);
};
