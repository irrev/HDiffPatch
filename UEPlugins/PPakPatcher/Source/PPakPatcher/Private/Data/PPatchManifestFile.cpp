// Copyright (c) Tencent. All rights reserved.
#include "Data/PPatchManifestFile.h"
#include "Data/PPakPatcherDataType.h"     // LogPPakPacher
#include "Data/PResPatchData.h"           // PRES_PATCH_FORMAT_VERSION
#include "PPakPatcherSettings.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"

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

	// ---- BuildSettings 辅助：enum 与字符串互转 ----

	const TCHAR* PakPatchModeToString(EPPakPatchMode InValue)
	{
		switch (InValue)
		{
		case EPPakPatchMode::Binary:                       return TEXT("Binary");
		case EPPakPatchMode::PakAwareNoDecrypt:            return TEXT("PakAwareNoDecrypt");
		case EPPakPatchMode::PakAwareDecryptAndCompress:   return TEXT("PakAwareDecryptAndCompress");
		case EPPakPatchMode::PakAwareDecryptAndDecompress: return TEXT("PakAwareDecryptAndDecompress");
		default:                                           return TEXT("Unknown");
		}
	}

	const TCHAR* ExternalCompressTypeToString(EPPatchExternalCompressType InValue)
	{
		switch (InValue)
		{
		case EPPatchExternalCompressType::None:            return TEXT("None");
		case EPPatchExternalCompressType::Oodle_Selkie:    return TEXT("Oodle_Selkie");
		case EPPatchExternalCompressType::Oodle_Mermaid:   return TEXT("Oodle_Mermaid");
		case EPPatchExternalCompressType::Oodle_Kraken:    return TEXT("Oodle_Kraken");
		case EPPatchExternalCompressType::Oodle_Leviathan: return TEXT("Oodle_Leviathan");
		default:                                           return TEXT("Unknown");
		}
	}

	const TCHAR* CheckFileHashTypeToString(EPPakCheckFileHashType InValue)
	{
		switch (InValue)
		{
		case EPPakCheckFileHashType::None:  return TEXT("None");
		case EPPakCheckFileHashType::Crc32: return TEXT("Crc32");
		case EPPakCheckFileHashType::MD5:   return TEXT("MD5");
		default:                            return TEXT("Unknown");
		}
	}

	bool ParseBoolField(const TSharedPtr<FJsonObject>& InObj, const FString& InField, bool InDefault)
	{
		bool Value = InDefault;
		if (InObj.IsValid()) { InObj->TryGetBoolField(InField, Value); }
		return Value;
	}

	int32 ParseInt32Field(const TSharedPtr<FJsonObject>& InObj, const FString& InField, int32 InDefault)
	{
		double D = 0.0;
		if (InObj.IsValid() && InObj->TryGetNumberField(InField, D))
		{
			return static_cast<int32>(D);
		}
		return InDefault;
	}

	// ---- BuildSettings 序列化 ----

	TSharedRef<FJsonObject> BuildSettingsToJson(const FPPatchBuildSettings& In)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();

		// 元信息
		Obj->SetStringField(TEXT("CreatedAtUtc"),       In.CreatedAtUtc);
		Obj->SetNumberField(TEXT("PatchFormatVersion"), static_cast<double>(In.PatchFormatVersion));
		Obj->SetStringField(TEXT("PluginVersion"),      In.PluginVersion);
		Obj->SetStringField(TEXT("HostMachine"),        In.HostMachine);

		// Mode / 预处理
		Obj->SetStringField(TEXT("PakPatchMode"),       In.PakPatchMode);

		// 外部压缩
		Obj->SetStringField(TEXT("ExternalCompressType"),  In.ExternalCompressType);
		Obj->SetNumberField(TEXT("ExternalCompressLevel"), static_cast<double>(In.ExternalCompressLevel));

		// HDiff
		Obj->SetBoolField  (TEXT("bUseSingleCompressMode"), In.bUseSingleCompressMode);
		Obj->SetNumberField(TEXT("MinSingleMatchScore"),    static_cast<double>(In.MinSingleMatchScore));
		Obj->SetBoolField  (TEXT("bUseBigCacheMatch"),      In.bUseBigCacheMatch);
		Obj->SetNumberField(TEXT("ThreadNum"),              static_cast<double>(In.ThreadNum));
		Obj->SetNumberField(TEXT("PatchStepMemSize"),       static_cast<double>(In.PatchStepMemSize));

		// 校验 / 行为开关
		Obj->SetStringField(TEXT("CheckFileHashType"),  In.CheckFileHashType);
		Obj->SetBoolField  (TEXT("bUsePerBlockDiff"),   In.bUsePerBlockDiff);
		Obj->SetBoolField  (TEXT("bDoubleCheckEntry"),  In.bDoubleCheckEntry);
		Obj->SetBoolField  (TEXT("bRecordSignToPatch"), In.bRecordSignToPatch);
		Obj->SetBoolField  (TEXT("bUseSignWriter"),     In.bUseSignWriter);
		Obj->SetBoolField  (TEXT("bGenPakFileMD5"),     In.bGenPakFileMD5);

		// pak 元数据各 block patch 开关
		Obj->SetBoolField(TEXT("bBinaryPatchIndexBlock"),         In.bBinaryPatchIndexBlock);
		Obj->SetBoolField(TEXT("bBinaryPatchPathHashBlock"),      In.bBinaryPatchPathHashBlock);
		Obj->SetBoolField(TEXT("bBinaryPatchFullDirectoryBlock"), In.bBinaryPatchFullDirectoryBlock);
		Obj->SetBoolField(TEXT("bBinaryPatchHeadBlock"),          In.bBinaryPatchHeadBlock);

		// precache
		Obj->SetBoolField(TEXT("bPrecachePatchDataOnSave"), In.bPrecachePatchDataOnSave);
		Obj->SetBoolField(TEXT("bPrecachePatchDataOnLoad"), In.bPrecachePatchDataOnLoad);

		// 任务级并发
		Obj->SetNumberField(TEXT("PatchTaskThreadNum"), static_cast<double>(In.PatchTaskThreadNum));

		return Obj;
	}

	FPPatchBuildSettings JsonToBuildSettings(const TSharedPtr<FJsonObject>& InObj)
	{
		FPPatchBuildSettings S;
		if (!InObj.IsValid()) return S;

		S.CreatedAtUtc       = ParseStringField(InObj, TEXT("CreatedAtUtc"));
		S.PatchFormatVersion = ParseInt32Field (InObj, TEXT("PatchFormatVersion"), 0);
		S.PluginVersion      = ParseStringField(InObj, TEXT("PluginVersion"));
		S.HostMachine        = ParseStringField(InObj, TEXT("HostMachine"));

		S.PakPatchMode = ParseStringField(InObj, TEXT("PakPatchMode"));

		S.ExternalCompressType  = ParseStringField(InObj, TEXT("ExternalCompressType"));
		S.ExternalCompressLevel = ParseInt32Field (InObj, TEXT("ExternalCompressLevel"), 0);

		S.bUseSingleCompressMode = ParseBoolField (InObj, TEXT("bUseSingleCompressMode"), true);
		S.MinSingleMatchScore    = ParseInt32Field(InObj, TEXT("MinSingleMatchScore"),    0);
		S.bUseBigCacheMatch      = ParseBoolField (InObj, TEXT("bUseBigCacheMatch"),      false);
		S.ThreadNum              = ParseInt32Field(InObj, TEXT("ThreadNum"),              0);
		S.PatchStepMemSize       = ParseInt32Field(InObj, TEXT("PatchStepMemSize"),       0);

		S.CheckFileHashType   = ParseStringField(InObj, TEXT("CheckFileHashType"));
		S.bUsePerBlockDiff    = ParseBoolField  (InObj, TEXT("bUsePerBlockDiff"),    false);
		S.bDoubleCheckEntry   = ParseBoolField  (InObj, TEXT("bDoubleCheckEntry"),   false);
		S.bRecordSignToPatch  = ParseBoolField  (InObj, TEXT("bRecordSignToPatch"),  false);
		S.bUseSignWriter      = ParseBoolField  (InObj, TEXT("bUseSignWriter"),      false);
		S.bGenPakFileMD5      = ParseBoolField  (InObj, TEXT("bGenPakFileMD5"),      false);

		S.bBinaryPatchIndexBlock         = ParseBoolField(InObj, TEXT("bBinaryPatchIndexBlock"),         false);
		S.bBinaryPatchPathHashBlock      = ParseBoolField(InObj, TEXT("bBinaryPatchPathHashBlock"),      false);
		S.bBinaryPatchFullDirectoryBlock = ParseBoolField(InObj, TEXT("bBinaryPatchFullDirectoryBlock"), false);
		S.bBinaryPatchHeadBlock          = ParseBoolField(InObj, TEXT("bBinaryPatchHeadBlock"),          false);

		S.bPrecachePatchDataOnSave = ParseBoolField(InObj, TEXT("bPrecachePatchDataOnSave"), false);
		S.bPrecachePatchDataOnLoad = ParseBoolField(InObj, TEXT("bPrecachePatchDataOnLoad"), false);

		S.PatchTaskThreadNum = ParseInt32Field(InObj, TEXT("PatchTaskThreadNum"), 0);

		return S;
	}
}

// ---- FPPatchBuildSettings ----

void FPPatchBuildSettings::FillFromCurrentSettings()
{
	const UPPakPatcherSettings& S = UPPakPatcherSettings::Get();

	// 元信息（CreatedAtUtc / PluginVersion / HostMachine 由 caller 在调用前后填，
	// 但这里也给一份兜底，便于直接调用 FillFromCurrentSettings 也得到完整快照）
	if (CreatedAtUtc.IsEmpty())
	{
		CreatedAtUtc = FDateTime::UtcNow().ToIso8601();
	}
	if (PatchFormatVersion == 0)
	{
		PatchFormatVersion = PRES_PATCH_FORMAT_VERSION;
	}
	if (HostMachine.IsEmpty())
	{
		HostMachine = FString::Printf(TEXT("%s/%s"),
			FPlatformProcess::ComputerName(),
			FPlatformProcess::UserName(false));
	}

	PakPatchMode = PakPatchModeToString(S.PakPatchMode);

	ExternalCompressType  = ExternalCompressTypeToString(S.ExternalCompressType);
	ExternalCompressLevel = S.ExternalCompressLevel;

	bUseSingleCompressMode = S.bUseSingleCompressMode;
	MinSingleMatchScore    = S.MinSingleMatchScore;
	bUseBigCacheMatch      = S.bUseBigCacheMatch;
	ThreadNum              = S.ThreadNum;
	PatchStepMemSize       = S.PatchStepMemSize;

	CheckFileHashType   = CheckFileHashTypeToString(S.CheckFileHashType);
	bUsePerBlockDiff    = S.bUsePerBlockDiff;
	bDoubleCheckEntry   = S.bDoubleCheckEntry;
	bRecordSignToPatch  = S.bRecordSignToPatch;
	bUseSignWriter      = S.bUseSignWriter;
	bGenPakFileMD5      = S.bGenPakFileMD5;

	bBinaryPatchIndexBlock         = S.bBinaryPatchIndexBlock;
	bBinaryPatchPathHashBlock      = S.bBinaryPatchPathHashBlock;
	bBinaryPatchFullDirectoryBlock = S.bBinaryPatchFullDirectoryBlock;
	bBinaryPatchHeadBlock          = S.bBinaryPatchHeadBlock;

	bPrecachePatchDataOnSave = S.bPrecachePatchDataOnSave;
	bPrecachePatchDataOnLoad = S.bPrecachePatchDataOnLoad;

	PatchTaskThreadNum = S.PatchTaskThreadNum;
}

void FPPatchManifestFile::Reset()
{
	SchemaVersion = PPATCH_MANIFEST_SCHEMA_VERSION;
	OldAppVersion.Empty();
	OldResVersion.Empty();
	NewAppVersion.Empty();
	NewResVersion.Empty();
	Platform.Empty();
	DolphinChannelID.Empty();
	PufferChannelID.Empty();
	BuildSettings = FPPatchBuildSettings{};
	ManifestFileItems.Empty();
	SourceFilename.Empty();
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

	// LoadFromString 内部会 Reset（含清 SourceFilename），所以解析后再回填 SourceFilename。
	const bool bOk = LoadFromString(JsonText);
	SourceFilename = InFilename;
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

	// BuildSettings：v2 schema 起的可选字段。v1 manifest 缺失时保持默认值（不报错）。
	const TSharedPtr<FJsonObject>* BuildSettingsObj = nullptr;
	if (Root->TryGetObjectField(TEXT("BuildSettings"), BuildSettingsObj) && BuildSettingsObj)
	{
		BuildSettings = JsonToBuildSettings(*BuildSettingsObj);
	}

	// SchemaVersion 兼容性：缺失视为 1（最早的 schema）；超出当前版本时警告但仍尝试解析
	int32 ParsedSchema = 1;
	if (Root->TryGetNumberField(TEXT("SchemaVersion"), ParsedSchema))
	{
		SchemaVersion = ParsedSchema;
		if (SchemaVersion > PPATCH_MANIFEST_SCHEMA_VERSION)
		{
			UE_LOG(LogPPakPacher, Warning,
				TEXT("FPPatchManifestFile::LoadFromString - manifest SchemaVersion=%d > current=%d. Will attempt parse with current code; some new fields may be ignored."),
				SchemaVersion, PPATCH_MANIFEST_SCHEMA_VERSION);
		}
	}
	else
	{
		SchemaVersion = 1; // 旧 manifest 没有此字段，按 v1 处理
	}

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

	// SchemaVersion 必须放在最前，便于人工排查文件
	Root->SetNumberField(TEXT("SchemaVersion"), PPATCH_MANIFEST_SCHEMA_VERSION);

	Root->SetStringField(TEXT("OldAppVersion"),    OldAppVersion);
	Root->SetStringField(TEXT("OldResVersion"),    OldResVersion);
	Root->SetStringField(TEXT("NewAppVersion"),    NewAppVersion);
	Root->SetStringField(TEXT("NewResVersion"),    NewResVersion);
	Root->SetStringField(TEXT("Platform"),         Platform);
	Root->SetStringField(TEXT("DolphinChannelID"), DolphinChannelID);
	Root->SetStringField(TEXT("PufferChannelID"),  PufferChannelID);

	// BuildSettings：v2 schema 起记录构建侧参数快照（仅诊断/审计，不影响 ApplyPatch）
	Root->SetObjectField(TEXT("BuildSettings"), BuildSettingsToJson(BuildSettings));

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
