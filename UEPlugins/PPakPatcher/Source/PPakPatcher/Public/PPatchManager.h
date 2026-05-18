// Copyright (c) Tencent. All rights reserved.
#pragma once

#include "CoreMinimal.h"

class FPUpdateManifestSummary;
class FPPatchManifestFile;

/**
 * 资源补丁的统一对外入口（构建侧 + 运行侧）。
 *
 * 四个核心 API：
 *   - CreatePatch       : 构建侧调用，根据新旧资源目录生成 .patch 文件 + patch_manifest.txt
 *   - ApplyPatch        : 运行侧调用，按 patch_manifest 把本地资源升级到新版本
 *   - VerifyBeforePatch : 运行侧调用，**打补丁前**校验本地 Old 资源 CRC 与 manifest 期望一致
 *   - VerifyAfterPatch  : 运行侧调用，**打补丁后**校验本地 New 资源 CRC 与 manifest 期望一致
 *
 * 校验函数的 CRC 设计：
 *   - 由外部（资源更新层）已经计算好的 {FileName -> CRC} 表传入，函数内部不读磁盘、不计算 CRC
 *   - 这样能避免重复计算（CRC 计算开销大，资源更新层下载完文件时通常已经算过一次）
 *
 * 单例由 TLazySingleton 持有；首次访问时构造，模块卸载时析构。
 *
 * 必备 manifest 文件：
 *   - <NewDir>/md5_file_list.txt    构建侧读取
 *   - <OldDir>/md5_file_list.txt    构建侧读取
 *   - <PatchDir>/patch_manifest.txt 本管理器在 CreatePatch 中产出，ApplyPatch/Verify* 时读取
 *   - <ResDir>/md5_file_list.txt    ApplyPatch 读取（Verify* 不读取，外部 CRC 表已涵盖）
 */
class PPAKPATCHER_API FPPatchManager
{
public:
	FPPatchManager() = default;
	~FPPatchManager() = default;

	/** 单例访问。 */
	static FPPatchManager& Get();

	/** 释放单例（一般由 Module 在 ShutdownModule 调用，可选）。 */
	static void TearDown();

	/** 当前固定的清单文件名常量。 */
	static const TCHAR* GetPatchManifestFileName()  { return TEXT("patch_manifest.txt"); }
	static const TCHAR* GetSourceManifestFileName() { return TEXT("md5_file_list.txt"); }

	// =====================================================================
	// 构建侧：生成补丁
	// =====================================================================

	/**
	 * 根据新旧资源目录生成补丁。
	 *   - 读 <InOldDir>/md5_file_list.txt 与 <InNewDir>/md5_file_list.txt 做版本/平台一致性检查
	 *   - 按 chunk 名称匹配新旧 .pak（IoStore 同伴文件由 FPPakPatcher 内部联动处理）
	 *   - 根据 New/Old MD5 自动判定 DiffType：Equal/Modify/Add/Delete
	 *   - Modify 项调 FPResPatcher::CreateDiff 生成 <NewBaseName>.patch
	 *   - Add    项把新文件直接拷入 InPatchDir
	 *   - 全部条目记录到 patch_manifest.txt 并保存
	 *
	 * @return true 表示全部条目处理成功；任一 Modify 失败即 false。
	 */
	bool CreatePatch(const FString& InOldDir, const FString& InNewDir, const FString& InPatchDir);

	// =====================================================================
	// 运行侧：应用补丁
	// =====================================================================

	/**
	 * 把 <InPatchDir> 中的补丁应用到 <InResDir>。
	 *   - 读 <InPatchDir>/patch_manifest.txt
	 *   - 读 <InResDir>/md5_file_list.txt
	 *   - 一致性检查：OldAppVersion / OldResVersion / Platform / DolphinChannelID / PufferChannelID
	 *     必须与本地资源 manifest 完全匹配，否则 fail
	 *   - 逐项处理：Modify -> PatchDiff；Add -> 拷贝；Delete -> 删除；Equal -> 跳过
	 *
	 * @return true 表示全部条目应用成功。
	 */
	bool ApplyPatch(const FString& InResDir, const FString& InPatchDir);

	// =====================================================================
	// 运行侧：补丁前/后的 CRC 校验（不读磁盘，纯比对）
	// =====================================================================

	/**
	 * 打补丁前：校验本地 Old 资源 CRC 与 patch_manifest 中 OldCRC 是否一致。
	 *
	 * 用途：
	 *   - 评估"是否值得打补丁"：若本地 Old 资源损坏或被篡改，PatchDiff 必定失败，
	 *     不如提前用本函数判定，让上层决定是"重新下载完整 New" 还是"打补丁"
	 *   - 与 ApplyPatch 解耦：本函数纯比对，无文件 IO，开销极低
	 *
	 * 校验对象：DiffType=Modify / Delete 条目的 Old 文件（这两类需要本地 Old 存在；
	 *           Add 此时本就没对应 Old；Equal 跳过——补丁前后都一样）
	 *
	 * @param InPatchDir       补丁目录（含 patch_manifest.txt）
	 * @param InActualCrcMap   外部已计算的 {FileName -> 实际 CRC}；典型来自资源更新层
	 * @param bAllowMissing    true：外部表缺失某条目时跳过（视为通过）；
	 *                         false（默认）：缺失即判失败
	 * @return true 表示本地 Old 资源完整、可以打补丁
	 */
	bool VerifyBeforePatch(const FString& InPatchDir,
		const TMap<FString /*FileName*/, uint32 /*CRC*/>& InActualCrcMap,
		bool bAllowMissing = false) const;

	/**
	 * 打补丁后：校验本地 New 资源 CRC 与 patch_manifest 中 NewCRC 是否一致。
	 *
	 * 用途：
	 *   - 确认补丁正确产出：ApplyPatch 内部已经做了 PatchDiff 各种校验，但仍可能因
	 *     磁盘异常/中断导致写盘不完整，本函数提供"最终一锤定音"的整体校验
	 *
	 * 校验对象：DiffType=Modify / Add / Equal 条目的 New 文件（这三类补丁后应有 New；
	 *           Delete 补丁后不应再有 Old，但当前 Delete 语义为 manifest-only 不做物理删除，故不校验）
	 *
	 * @param InPatchDir       补丁目录（含 patch_manifest.txt）
	 * @param InActualCrcMap   外部已计算的 {FileName -> 实际 CRC}
	 * @param bAllowMissing    true：外部表缺失某条目时跳过；false（默认）：缺失即判失败
	 * @return true 表示补丁应用正确、本地资源已是新版本
	 */
	bool VerifyAfterPatch(const FString& InPatchDir,
		const TMap<FString /*FileName*/, uint32 /*CRC*/>& InActualCrcMap,
		bool bAllowMissing = false) const;

private:
	/** 一致性检查：patch 与本地资源在版本/平台/渠道上是否匹配。 */
	bool CheckCompatibility(const FPPatchManifestFile& InPatchManifest,
		const FPUpdateManifestSummary& InResSummary) const;

	/** chunk 名匹配：从 New 文件名推出 chunk 名（如 "pakchunk0_1.19.9.3_3_P.pak" -> "pakchunk0"）。 */
	static FString ExtractChunkName(const FString& InFilename);

	/** 反查 OldDir 中同 chunk 名的文件路径；找不到返回空。 */
	static FString FindOldFileForNew(const FString& InNewFileName,
		const TMap<FString, FString>& InOldFileMap /*chunkName -> filename*/);

	/**
	 * 共用 CRC 校验逻辑：根据"取哪一边的 FileName / 哪一边的 CRC"参数化。
	 *   bUseNewSide=true  -> 校验 NewFileName + NewCRC（VerifyAfterPatch）
	 *   bUseNewSide=false -> 校验 OldFileName + OldCRC（VerifyBeforePatch）
	 */
	bool VerifyCrcInternal(const FString& InPatchDir,
		const TMap<FString, uint32>& InActualCrcMap,
		bool bAllowMissing,
		bool bUseNewSide,
		const TCHAR* InTagForLog) const;
};
