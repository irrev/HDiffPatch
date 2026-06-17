// Copyright (c) Tencent. All rights reserved.
#include "Data/PUpdateManifestSummary.h"
#include "Data/PPakPatcherDataType.h"          // LogPPakPacher

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	/**
	 * 从 JSON 对象里取一个字段，支持把 "true"/"false" 字符串解析为 bool（兼容
	 * md5_file_list.txt 把 VERSION_NEW_APP 写成字符串的写法）。字段缺失返回 InDefault。
	 */
	bool ParseBoolField(const TSharedPtr<FJsonObject>& InObj, const FString& InField, bool InDefault)
	{
		if (!InObj.IsValid())
		{
			return InDefault;
		}

		// 先按 string 解析（md5_file_list.txt 的写法）
		FString StrValue;
		if (InObj->TryGetStringField(InField, StrValue))
		{
			return StrValue.Equals(TEXT("true"), ESearchCase::IgnoreCase);
		}

		// 退而求其次：按 bool 解析
		bool BoolValue = InDefault;
		if (InObj->TryGetBoolField(InField, BoolValue))
		{
			return BoolValue;
		}
		return InDefault;
	}

	/** 取字符串字段，缺失返回空串。 */
	FString ParseStringField(const TSharedPtr<FJsonObject>& InObj, const FString& InField)
	{
		FString Value;
		if (InObj.IsValid())
		{
			InObj->TryGetStringField(InField, Value);
		}
		return Value;
	}

	/** 解析一个 FileList 元素。 */
	bool ParseFileItem(const TSharedPtr<FJsonObject>& InObj, FPUpdateManifestSummaryItem& OutItem)
	{
		if (!InObj.IsValid())
		{
			return false;
		}

		if (!InObj->TryGetStringField(TEXT("FileName"), OutItem.FileName) || OutItem.FileName.IsEmpty())
		{
			return false;
		}

		InObj->TryGetStringField(TEXT("MD5"),  OutItem.MD5);
		InObj->TryGetStringField(TEXT("Type"), OutItem.Type);
		InObj->TryGetNumberField(TEXT("CRC"), OutItem.CRC);
		InObj->TryGetNumberField(TEXT("Size"), OutItem.Size);

		return true;
	}
}

void FPUpdateManifestSummary::Reset()
{
	bIsNewApp = false;
	bIsNewRes = false;
	AppVersion.Empty();
	ResVersion.Empty();
	Platform.Empty();
	PatchBaseVersion.Empty();
	ResVersionLinks.Empty();
	AppHash.Empty();
	DolphinChannelID.Empty();
	PufferChannelID.Empty();
	ManifestFileItems.Empty();
	SourceFilename.Empty();
}

bool FPUpdateManifestSummary::Load(const FString& InFilename)
{
	if (InFilename.IsEmpty())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPUpdateManifestSummary::Load - empty filename."));
		return false;
	}

	if (!IFileManager::Get().FileExists(*InFilename))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPUpdateManifestSummary::Load - file not found: %s"), *InFilename);
		return false;
	}

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *InFilename))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPUpdateManifestSummary::Load - failed to read file: %s"), *InFilename);
		return false;
	}

	// 缓存来源文件名（LoadFromString 内部会 Reset 清空它，所以解析完成后再回填）；
	// 解析失败时也要回填，方便错误日志展示来源
	const bool bOk = LoadFromString(JsonText);
	SourceFilename = InFilename;
	if (bOk)
	{
		UE_LOG(LogPPakPacher, Display,
			TEXT("FPUpdateManifestSummary::Load - Loaded: %s. AppVersion=%s ResVersion=%s Platform=%s Files=%d"),
			*InFilename, *AppVersion, *ResVersion, *Platform,
			ManifestFileItems.Num());
	}
	return bOk;
}

bool FPUpdateManifestSummary::LoadFromString(const FString& InJsonText)
{
	Reset();

	if (InJsonText.IsEmpty())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPUpdateManifestSummary::LoadFromString - empty json text."));
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(InJsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPUpdateManifestSummary::LoadFromString - JSON deserialize failed. Source:%s"),
			*SourceFilename);
		return false;
	}

	// ---- 顶层元信息 ----
	bIsNewApp        = ParseBoolField(Root,   TEXT("VERSION_NEW_APP"),  false);
	bIsNewRes        = ParseBoolField(Root,   TEXT("VERSION_NEW_RES"),  false);
	AppVersion       = ParseStringField(Root, TEXT("VERSION_APP"));
	ResVersion       = ParseStringField(Root, TEXT("VERSION_RES"));
	Platform         = ParseStringField(Root, TEXT("COOKED_PLATFORM"));
	PatchBaseVersion = ParseStringField(Root, TEXT("VERSION_PATCH_BASE"));
	ResVersionLinks  = ParseStringField(Root, TEXT("VERSION_RES_LINKS"));
	AppHash          = ParseStringField(Root, TEXT("VERSION_APP_HASH"));
	DolphinChannelID = ParseStringField(Root, TEXT("DOLPHIN_CHANNEL_ID"));
	PufferChannelID  = ParseStringField(Root, TEXT("PUFFER_CHANNEL_ID"));

	// ---- FileList ----
	const TArray<TSharedPtr<FJsonValue>>* FileListArray = nullptr;
	if (Root->TryGetArrayField(TEXT("FileList"), FileListArray) && FileListArray)
	{
		ManifestFileItems.Reserve(FileListArray->Num());
		for (const TSharedPtr<FJsonValue>& Value : *FileListArray)
		{
			if (!Value.IsValid() || Value->Type != EJson::Object)
			{
				continue;
			}
			FPUpdateManifestSummaryItem Item;
			if (ParseFileItem(Value->AsObject(), Item))
			{
				// 同名文件以最后一项为准（理论上不会重复）；命中重复时打 warning 便于排查
				if (ManifestFileItems.Contains(Item.FileName))
				{
					UE_LOG(LogPPakPacher, Warning,
						TEXT("FPUpdateManifestSummary - duplicate FileName in FileList: %s (later overrides earlier)"),
						*Item.FileName);
				}
				ManifestFileItems.Add(Item.FileName, MoveTemp(Item));
			}
		}
	}
	else
	{
		UE_LOG(LogPPakPacher, Warning, TEXT("FPUpdateManifestSummary - missing or invalid 'FileList' array. Source:%s"),
			*SourceFilename);
	}

	// ChunkerInfo 字段存在于原始 md5_file_list.txt 中（分包下载元信息），
	// 但 PPakPatcher 补丁流程不需要，直接跳过。

	return true;
}
