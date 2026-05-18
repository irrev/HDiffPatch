// Copyright (c) Tencent. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "Data/PPakPatcherDataType.h"   // EPFileCompareDiffType

/**
 * 单个文件的补丁描述（子结构）。
 * 一个 chunk 内可有 .pak / .utoc / .ucas 三个文件，各自独立标记 DiffType。
 */
struct PPAKPATCHER_API FPPatchManifestFileEntry
{
	FString FileName;    // 文件名（不含路径，如 "pakchunk1100_0.pak"）

	EPFileCompareDiffType DiffType = EPFileCompareDiffType::None;

	FString OldMD5;
	FString NewMD5;

	uint32 OldCRC = 0;
	uint32 NewCRC = 0;

	int64 OldSize = 0;
	int64 NewSize = 0;

	bool IsValid() const { return !FileName.IsEmpty() && DiffType != EPFileCompareDiffType::None; }
};

/**
 * patch_manifest.txt 中 FileList[] 的一个条目（chunk 级）。
 *
 * 一条条目描述一个 chunk 的补丁信息：
 *   - ChunkName  : chunk 标识符（如 "pakchunk1100"）
 *   - PatchFileName : .patch 文件名（Modify 时有效；Add/Delete 类型为 Add 文件名或空）
 *   - Pak / Utoc / Ucas : 各子文件的独立 DiffType + hash。
 *     - Equal  : 文件没变，不做操作
 *     - Modify : 用 patch 数据做增量回放
 *     - Add    : 从 PatchDir 拷贝新文件
 *     - Delete : 从 ResDir 删除
 *     - None   : 该同伴文件不存在（如纯 Pak 没有 IoStore）
 */
class PPAKPATCHER_API FPPatchManifestFileItem
{
public:
	FString ChunkName;      // chunk 标识符
	FString PatchFileName;  // .patch 文件名（Modify 时使用）

	FPPatchManifestFileEntry Pak;
	FPPatchManifestFileEntry Utoc;
	FPPatchManifestFileEntry Ucas;

	/** 获取 chunk 的整体 DiffType（取三个文件中最"重"的） */
	EPFileCompareDiffType GetChunkDiffType() const
	{
		// 优先级：Modify > Add > Delete > Equal > None
		auto Priority = [](EPFileCompareDiffType T) -> int32 {
			switch (T) {
			case EPFileCompareDiffType::Modify: return 4;
			case EPFileCompareDiffType::Add:    return 3;
			case EPFileCompareDiffType::Delete: return 2;
			case EPFileCompareDiffType::Equal:  return 1;
			default:                            return 0;
			}
		};
		EPFileCompareDiffType Result = EPFileCompareDiffType::None;
		for (const auto* Entry : { &Pak, &Utoc, &Ucas })
		{
			if (Priority(Entry->DiffType) > Priority(Result))
			{
				Result = Entry->DiffType;
			}
		}
		return Result;
	}

	/** 便捷：获取 .pak 的 NewFileName（向后兼容旧逻辑引用 Item.NewFileName 的地方） */
	const FString& GetPakFileName() const { return Pak.FileName; }
};

/**
 * patch_manifest.txt 的内存结构。
 *
 * 创建补丁时（FPPatchManager::CreatePatch）生成并保存到 PatchDir/patch_manifest.txt；
 * 应用补丁时（FPPatchManager::ApplyPatch）从 PatchDir 读取。
 *
 * 物理文件格式：UTF-8 JSON。
 */
class PPAKPATCHER_API FPPatchManifestFile
{
public:
	FPPatchManifestFile() = default;
	~FPPatchManifestFile() = default;

	bool Load(const FString& InFilename);
	bool Save(const FString& InFilename) const;

	bool LoadFromString(const FString& InJsonText);
	bool SaveToString(FString& OutJsonText) const;

	/** 文件项查询：以 ChunkName 为 key。 */
	const TMap<FString, FPPatchManifestFileItem>& GetManifestFileItems() const { return ManifestFileItems; }
	TMap<FString, FPPatchManifestFileItem>&       GetManifestFileItems()       { return ManifestFileItems; }

	const FPPatchManifestFileItem* FindByChunkName(const FString& InChunkName) const { return ManifestFileItems.Find(InChunkName); }

	/** 添加/覆盖一项。Key 取自 InItem.ChunkName。 */
	void AddItem(const FPPatchManifestFileItem& InItem);

	/** 清空所有数据。 */
	void Reset();

public:
	// ---- 顶层元信息 ----

	FString OldAppVersion;
	FString OldResVersion;

	FString NewAppVersion;
	FString NewResVersion;

	FString Platform;
	FString DolphinChannelID;
	FString PufferChannelID;

	/** 加载来源文件路径（仅记录，不参与序列化）。 */
	FString SourceFilename;

private:
	TMap<FString, FPPatchManifestFileItem> ManifestFileItems;
};
