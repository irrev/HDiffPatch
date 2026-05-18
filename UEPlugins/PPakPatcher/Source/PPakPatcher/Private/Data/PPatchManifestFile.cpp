// Copyright (c) Tencent. All rights reserved.
#include "Data/PPatchManifestFile.h"
#include "Data/PPakPatcherDataType.h"     // LogPPakPacher

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/PrettyJsonPrintPolicy.h"

namespace
{
	const TCHAR* DiffTypeToString(EPFileCompareDiffType InValue)
	{
		switch (InValue)
		{
		case EPFileCompareDiffType::None:    return TEXT("None");
		case EPFileCompareDiffType::Equal:   return TEXT("Equal");
		case EPFileCompareDiffType::Add:     return TEXT("Add");
		case EPFileCompareDiffType::Delete:  return TEXT("Delete");
		case EPFileCompareDiffType::Modify:  return TEXT("Modify");
		default:                             return TEXT("None");
		}
	}

	EPFileCompareDiffType StringToDiffType(const FString& InValue)
	{
		if (InValue.Equals(TEXT("Equal"),  ESearchCase::IgnoreCase)) return EPFileCompareDiffType::Equal;
		if (InValue.Equals(TEXT("Add"),    ESearchCase::IgnoreCase)) return EPFileCompareDiffType::Add;
		if (InValue.Equals(TEXT("Delete"), ESearchCase::IgnoreCase)) return EPFileCompareDiffType::Delete;
		if (InValue.Equals(TEXT("Modify"), ESearchCase::IgnoreCase)) return EPFileCompareDiffType::Modify;
		return EPFileCompareDiffType::None;
	}

	FString ParseStringField(const TSharedPtr<FJsonObject>& InObj, const FString& InField)
	{
		FString Value;
		if (InObj.IsValid()) { InObj->TryGetStringField(InField, Value); }
		return Value;
	}

	uint32 ParseUInt32Field(const TSharedPtr<FJsonObject>& InObj, const FString& InField)
	{
		double D = 0.0;
		if (InObj.IsValid() && InObj->TryGetNumberField(InField, D))
		{
			if (D < 0.0)                D = 0.0;
			if (D > (double)MAX_uint32) D = (double)MAX_uint32;
			return static_cast<uint32>(D);
		}
		return 0;
	}

	int64 ParseInt64Field(const TSharedPtr<FJsonObject>& InObj, const FString& InField)
	{
		double D = 0.0;
		if (InObj.IsValid() && InObj->TryGetNumberField(InField, D))
		{
			return static_cast<int64>(D);
		}
		return 0;
	}

	// ---- Entry 序列化/反序列化 ----

	TSharedRef<FJsonObject> EntryToJson(const FPPatchManifestFileEntry& InEntry)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("FileName"), InEntry.FileName);
		Obj->SetStringField(TEXT("DiffType"), DiffTypeToString(InEntry.DiffType));
		Obj->SetStringField(TEXT("OldMD5"),   InEntry.OldMD5);
		Obj->SetStringField(TEXT("NewMD5"),   InEntry.NewMD5);
		Obj->SetNumberField(TEXT("OldCRC"),   static_cast<double>(InEntry.OldCRC));
		Obj->SetNumberField(TEXT("NewCRC"),   static_cast<double>(InEntry.NewCRC));
		Obj->SetNumberField(TEXT("OldSize"),  static_cast<double>(InEntry.OldSize));
		Obj->SetNumberField(TEXT("NewSize"),  static_cast<double>(InEntry.NewSize));
		return Obj;
	}

	FPPatchManifestFileEntry JsonToEntry(const TSharedPtr<FJsonObject>& InObj)
	{
		FPPatchManifestFileEntry Entry;
		if (!InObj.IsValid()) return Entry;
		Entry.FileName = ParseStringField(InObj, TEXT("FileName"));
		Entry.DiffType = StringToDiffType(ParseStringField(InObj, TEXT("DiffType")));
		Entry.OldMD5   = ParseStringField(InObj, TEXT("OldMD5"));
		Entry.NewMD5   = ParseStringField(InObj, TEXT("NewMD5"));
		Entry.OldCRC   = ParseUInt32Field(InObj, TEXT("OldCRC"));
		Entry.NewCRC   = ParseUInt32Field(InObj, TEXT("NewCRC"));
		Entry.OldSize  = ParseInt64Field(InObj,  TEXT("OldSize"));
		Entry.NewSize  = ParseInt64Field(InObj,  TEXT("NewSize"));
		return Entry;
	}
}

void FPPatchManifestFile::Reset()
{
	OldAppVersion.Empty();
	OldResVersion.Empty();
	NewAppVersion.Empty();
	NewResVersion.Empty();
	Platform.Empty();
	DolphinChannelID.Empty();
	PufferChannelID.Empty();
	ManifestFileItems.Empty();
}

void FPPatchManifestFile::AddItem(const FPPatchManifestFileItem& InItem)
{
	if (InItem.ChunkName.IsEmpty())
	{
		UE_LOG(LogPPakPacher, Warning, TEXT("FPPatchManifestFile::AddItem - ChunkName is empty, skip."));
		return;
	}
	ManifestFileItems.Add(InItem.ChunkName, InItem);
}

bool FPPatchManifestFile::Load(const FString& InFilename)
{
	if (InFilename.IsEmpty())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPatchManifestFile::Load - empty filename."));
		return false;
	}

	if (!IFileManager::Get().FileExists(*InFilename))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPatchManifestFile::Load - file not found: %s"), *InFilename);
		return false;
	}

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *InFilename))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPatchManifestFile::Load - failed to read file: %s"), *InFilename);
		return false;
	}

	SourceFilename = InFilename;
	const bool bOk = LoadFromString(JsonText);
	if (bOk)
	{
		UE_LOG(LogPPakPacher, Display,
			TEXT("FPPatchManifestFile::Load - Loaded: %s. Old=[%s/%s] New=[%s/%s] Platform=%s Chunks=%d"),
			*InFilename, *OldAppVersion, *OldResVersion, *NewAppVersion, *NewResVersion, *Platform,
			ManifestFileItems.Num());
	}
	return bOk;
}

bool FPPatchManifestFile::LoadFromString(const FString& InJsonText)
{
	Reset();

	if (InJsonText.IsEmpty())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPatchManifestFile::LoadFromString - empty json text."));
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(InJsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPatchManifestFile::LoadFromString - JSON deserialize failed. Source:%s"),
			*SourceFilename);
		return false;
	}

	OldAppVersion    = ParseStringField(Root, TEXT("OldAppVersion"));
	OldResVersion    = ParseStringField(Root, TEXT("OldResVersion"));
	NewAppVersion    = ParseStringField(Root, TEXT("NewAppVersion"));
	NewResVersion    = ParseStringField(Root, TEXT("NewResVersion"));
	Platform         = ParseStringField(Root, TEXT("Platform"));
	DolphinChannelID = ParseStringField(Root, TEXT("DolphinChannelID"));
	PufferChannelID  = ParseStringField(Root, TEXT("PufferChannelID"));

	const TArray<TSharedPtr<FJsonValue>>* FileListArray = nullptr;
	if (Root->TryGetArrayField(TEXT("FileList"), FileListArray) && FileListArray)
	{
		ManifestFileItems.Reserve(FileListArray->Num());
		for (const TSharedPtr<FJsonValue>& Value : *FileListArray)
		{
			if (!Value.IsValid() || Value->Type != EJson::Object) continue;
			const TSharedPtr<FJsonObject> Obj = Value->AsObject();

			FPPatchManifestFileItem Item;
			Item.ChunkName     = ParseStringField(Obj, TEXT("ChunkName"));
			Item.PatchFileName = ParseStringField(Obj, TEXT("PatchFileName"));

			// 读子文件 Entry
			const TSharedPtr<FJsonObject>* PakObj = nullptr;
			if (Obj->TryGetObjectField(TEXT("Pak"), PakObj) && PakObj)
				Item.Pak = JsonToEntry(*PakObj);

			const TSharedPtr<FJsonObject>* UtocObj = nullptr;
			if (Obj->TryGetObjectField(TEXT("Utoc"), UtocObj) && UtocObj)
				Item.Utoc = JsonToEntry(*UtocObj);

			const TSharedPtr<FJsonObject>* UcasObj = nullptr;
			if (Obj->TryGetObjectField(TEXT("Ucas"), UcasObj) && UcasObj)
				Item.Ucas = JsonToEntry(*UcasObj);

			AddItem(Item);
		}
	}
	else
	{
		UE_LOG(LogPPakPacher, Warning,
			TEXT("FPPatchManifestFile::LoadFromString - missing or invalid 'FileList' array. Source:%s"),
			*SourceFilename);
	}

	return true;
}

bool FPPatchManifestFile::SaveToString(FString& OutJsonText) const
{
	OutJsonText.Empty();

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();

	Root->SetStringField(TEXT("OldAppVersion"),    OldAppVersion);
	Root->SetStringField(TEXT("OldResVersion"),    OldResVersion);
	Root->SetStringField(TEXT("NewAppVersion"),    NewAppVersion);
	Root->SetStringField(TEXT("NewResVersion"),    NewResVersion);
	Root->SetStringField(TEXT("Platform"),         Platform);
	Root->SetStringField(TEXT("DolphinChannelID"), DolphinChannelID);
	Root->SetStringField(TEXT("PufferChannelID"),  PufferChannelID);

	TArray<TSharedPtr<FJsonValue>> FileListArray;
	FileListArray.Reserve(ManifestFileItems.Num());

	// 按 key 字母序输出，便于 diff
	TArray<FString> SortedKeys;
	ManifestFileItems.GetKeys(SortedKeys);
	SortedKeys.Sort();

	for (const FString& Key : SortedKeys)
	{
		const FPPatchManifestFileItem& Item = ManifestFileItems.FindChecked(Key);

		TSharedRef<FJsonObject> ItemObj = MakeShared<FJsonObject>();
		ItemObj->SetStringField(TEXT("ChunkName"),     Item.ChunkName);
		ItemObj->SetStringField(TEXT("PatchFileName"), Item.PatchFileName);

		// 只输出有效的 Entry（DiffType != None）
		if (Item.Pak.DiffType != EPFileCompareDiffType::None)
			ItemObj->SetObjectField(TEXT("Pak"), EntryToJson(Item.Pak));
		if (Item.Utoc.DiffType != EPFileCompareDiffType::None)
			ItemObj->SetObjectField(TEXT("Utoc"), EntryToJson(Item.Utoc));
		if (Item.Ucas.DiffType != EPFileCompareDiffType::None)
			ItemObj->SetObjectField(TEXT("Ucas"), EntryToJson(Item.Ucas));

		FileListArray.Add(MakeShared<FJsonValueObject>(ItemObj));
	}

	Root->SetArrayField(TEXT("FileList"), FileListArray);

	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer
		= TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutJsonText);
	return FJsonSerializer::Serialize(Root, Writer);
}

bool FPPatchManifestFile::Save(const FString& InFilename) const
{
	if (InFilename.IsEmpty())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPatchManifestFile::Save - empty filename."));
		return false;
	}

	FString JsonText;
	if (!SaveToString(JsonText))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPatchManifestFile::Save - serialize failed. Filename:%s"), *InFilename);
		return false;
	}

	if (!FFileHelper::SaveStringToFile(JsonText, *InFilename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPatchManifestFile::Save - write failed. Filename:%s"), *InFilename);
		return false;
	}

	UE_LOG(LogPPakPacher, Display,
		TEXT("FPPatchManifestFile::Save - Saved: %s. Chunks=%d"), *InFilename, ManifestFileItems.Num());
	return true;
}
