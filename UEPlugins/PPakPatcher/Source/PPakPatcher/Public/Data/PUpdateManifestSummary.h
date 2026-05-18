// Copyright (c) Tencent. All rights reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * 单个补丁产物文件的摘要项。对应 md5_file_list.txt 中 "FileList[i]" 的一行。
 *
 * JSON 形如：
 *   {
 *     "FileName": "pakchunk0_1.19.9.2_2_P.pak",
 *     "MD5":      "b698404b555f758408420cb6e6979555",
 *     "CRC":      986074116,          // uint32（JSON 数字；可能 > INT32_MAX）
 *     "Type":     "Puffer",            // 当前已知值：Puffer；保留为字符串以容纳未来扩展
 *     "Size":     339                  // 字节数
 *   }
 */
class PPAKPATCHER_API FPUpdateManifestSummaryItem
{
public:
	FString FileName;
	FString MD5;
	uint32  CRC  = 0;
	FString Type;
	int64   Size = 0;
};

/**
 * 一个版本的补丁产物清单（每个版本目录下都会有一份 md5_file_list.txt）。
 *
 * 顶层 JSON 形如：
 *   {
 *     "VERSION_NEW_APP":   "false",
 *     "VERSION_NEW_RES":   "true",
 *     "VERSION_APP":       "1.19.9.0",
 *     "VERSION_RES":       "1.19.9.3",
 *     "COOKED_PLATFORM":   "Android_ASTC",
 *     "VERSION_PATCH_BASE":"1.19.9.2",
 *     "VERSION_RES_LINKS": "",
 *     "VERSION_APP_HASH":  "",
 *     "DOLPHIN_CHANNEL_ID":"2029813312",
 *     "PUFFER_CHANNEL_ID": "1465422334",
 *     "FileList":    [ { ... } ]
 *   }
 *
 * 注：原始 md5_file_list.txt 中还有 "ChunkerInfo" 字段（分包下载元信息），
 * 但 PPakPatcher 的补丁流程不需要它，加载时直接跳过。
 *
 * 用法：
 *   FPUpdateManifestSummary M;
 *   if (M.Load(TEXT("D:/.../1.19.9.3_Puffer/md5_file_list.txt"))) { ... }
 *   const FPUpdateManifestSummaryItem* Item = M.FindFile(TEXT("pakchunk0_1.19.9.2_2_P.pak"));
 */
class PPAKPATCHER_API FPUpdateManifestSummary
{
public:
	FPUpdateManifestSummary() = default;
	~FPUpdateManifestSummary() = default;

	/** 从 JSON 文件加载；任何字段缺失/解析失败均不致命，会以默认值填充并打 warning。 */
	bool Load(const FString& InFilename);

	/** 从已读入内存的 JSON 文本加载（便于单测/网络下载场景）。 */
	bool LoadFromString(const FString& InJsonText);

	/** 文件项查询：以 FileName 为 key。 */
	const TMap<FString, FPUpdateManifestSummaryItem>& GetManifestFileItems() const { return ManifestFileItems; }
	const FPUpdateManifestSummaryItem* FindFile(const FString& InFileName) const { return ManifestFileItems.Find(InFileName); }

	/** 清空所有数据（不含 SourceFilename）。 */
	void Reset();

public:
	// ---- 顶层元信息（直接公开读写，便于运行时按需访问/序列化） ----

	/** 是否携带新 App 包（VERSION_NEW_APP，"true"/"false"）。 */
	bool bIsNewApp = false;

	/** 是否携带新资源（VERSION_NEW_RES，"true"/"false"）。 */
	bool bIsNewRes = false;

	/** App 版本号（VERSION_APP）。 */
	FString AppVersion;

	/** 资源版本号（VERSION_RES）。 */
	FString ResVersion;

	/** Cook 平台标识（COOKED_PLATFORM，例如 Android_ASTC / WindowsClient）。 */
	FString Platform;

	/** 增量补丁的基础资源版本（VERSION_PATCH_BASE）。 */
	FString PatchBaseVersion;

	/** 资源版本链路（VERSION_RES_LINKS，可为空字符串）。 */
	FString ResVersionLinks;

	/** App 哈希（VERSION_APP_HASH，可为空字符串）。 */
	FString AppHash;

	/** 渠道 ID（DOLPHIN_CHANNEL_ID）。 */
	FString DolphinChannelID;

	/** 渠道 ID（PUFFER_CHANNEL_ID）。 */
	FString PufferChannelID;

	/** 加载来源文件路径（仅记录，不参与序列化）。 */
	FString SourceFilename;

private:
	TMap<FString, FPUpdateManifestSummaryItem> ManifestFileItems;
};
