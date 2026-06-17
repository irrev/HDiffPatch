
#include "PPakPatcherCommandlet.h"
#include "Engine.h"
#include "PPakPatcherModule.h"
#include "PPakPatcherSettings.h"
#include "PPatchManager.h"
#include "Data/PUpdateManifestSummary.h"
#include "Patcher/FPResPatcher.h"
#include "UnitTest/PPakPatcherUnitTest.h"
#include "Utils/PPakPatcherUtils.h"
#include "Data/PPakPatcherKeyChainHelper.h"
#include "Misc/KeyChainUtilities.h"


DEFINE_LOG_CATEGORY_STATIC(LogPakPatcherCommandlet, Display, All);

namespace
{
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

	template <typename T>
	void LogOverrideOrIni(const TCHAR* InName, const TOptional<T>& InOpt, const FString& InValueAsString)
	{
		if (InOpt.IsSet())
		{
			UE_LOG(LogPakPatcherCommandlet, Display, TEXT("%s (override): %s"), InName, *InValueAsString);
		}
		else
		{
			UE_LOG(LogPakPatcherCommandlet, Display, TEXT("%s: <use Settings ini value>"), InName);
		}
	}
}

void FPPakPatcherCommandletParams::Parse(const FString& Params)
{
	FString Value;
	if (FParse::Value(*Params, TEXT("CompressType="), Value))
	{
		Value = Value.TrimStartAndEnd();
		if (Value.Equals(TEXT("None"), ESearchCase::IgnoreCase)) CompressType = EPakPatchCompressType::None;
        else if (Value.Equals(TEXT("ZLIB"), ESearchCase::IgnoreCase)) CompressType = EPakPatchCompressType::ZLIB;
        else if (Value.Equals(TEXT("LZMA"), ESearchCase::IgnoreCase)) CompressType = EPakPatchCompressType::LZMA;
        else if (Value.Equals(TEXT("LZMA2"), ESearchCase::IgnoreCase)) CompressType = EPakPatchCompressType::LZMA2;
        else if (Value.Equals(TEXT("ZSTD"), ESearchCase::IgnoreCase)) CompressType = EPakPatchCompressType::ZSTD;
        else if (Value.Equals(TEXT("LDEF"), ESearchCase::IgnoreCase)) CompressType = EPakPatchCompressType::LDEF;
        else if (Value.Equals(TEXT("BZ2"), ESearchCase::IgnoreCase)) CompressType = EPakPatchCompressType::BZ2;
	}
	if (FParse::Value(*Params, TEXT("Mode="), Value))
	{
		Value = Value.TrimStartAndEnd();
		if      (Value.Equals(TEXT("Binary"),                       ESearchCase::IgnoreCase)) PatchMode = EPPakPatchMode::Binary;
		else if (Value.Equals(TEXT("PakAwareNoDecrypt"),            ESearchCase::IgnoreCase)) PatchMode = EPPakPatchMode::PakAwareNoDecrypt;
		else if (Value.Equals(TEXT("PakAwareDecryptAndCompress"),   ESearchCase::IgnoreCase)) PatchMode = EPPakPatchMode::PakAwareDecryptAndCompress;
		else if (Value.Equals(TEXT("PakAwareDecryptAndDecompress"), ESearchCase::IgnoreCase)) PatchMode = EPPakPatchMode::PakAwareDecryptAndDecompress;
		// 简写别名（向后兼容旧测试脚本 / 习惯）
		else if (Value.Equals(TEXT("NoDecrypt"),                    ESearchCase::IgnoreCase)) PatchMode = EPPakPatchMode::PakAwareNoDecrypt;
		else if (Value.Equals(TEXT("DecryptAndCompress"),           ESearchCase::IgnoreCase)) PatchMode = EPPakPatchMode::PakAwareDecryptAndCompress;
		else if (Value.Equals(TEXT("DecryptAndDecompress"),         ESearchCase::IgnoreCase)) PatchMode = EPPakPatchMode::PakAwareDecryptAndDecompress;
		else if (Value.Equals(TEXT("PakAware"),                     ESearchCase::IgnoreCase)) PatchMode = EPPakPatchMode::PakAwareDecryptAndCompress;	// 旧值别名 → 默认 DAC
		else
		{
			UE_LOG(LogPakPatcherCommandlet, Warning,
				TEXT("Unknown -Mode value: %s. Allowed: Binary | PakAwareNoDecrypt | PakAwareDecryptAndCompress | PakAwareDecryptAndDecompress"),
				*Value);
		}
	}
	if (FParse::Value(*Params, TEXT("CheckMode="), Value))
	{
		Value = Value.TrimStartAndEnd();
		if (Value.Equals(TEXT("PatchAndCompare"), ESearchCase::IgnoreCase)) CheckMode = EPPakUnitTestCheckMode::PatchAndCompare;
        else if (Value.Equals(TEXT("HDiffCheck"), ESearchCase::IgnoreCase)) CheckMode = EPPakUnitTestCheckMode::HDiffCheck;
        else if (Value.Equals(TEXT("Both"), ESearchCase::IgnoreCase)) CheckMode = EPPakUnitTestCheckMode::Both;
	}

	// -ExternalCompressType=None|Oodle_Selkie|Oodle_Mermaid|Oodle_Kraken|Oodle_Leviathan
	if (FParse::Value(*Params, TEXT("ExternalCompressType="), Value))
	{
		Value = Value.TrimStartAndEnd();
		if      (Value.Equals(TEXT("None"),            ESearchCase::IgnoreCase)) ExternalCompressTypeOverride = EPPatchExternalCompressType::None;
		else if (Value.Equals(TEXT("Oodle_Selkie"),    ESearchCase::IgnoreCase)) ExternalCompressTypeOverride = EPPatchExternalCompressType::Oodle_Selkie;
		else if (Value.Equals(TEXT("Oodle_Mermaid"),   ESearchCase::IgnoreCase)) ExternalCompressTypeOverride = EPPatchExternalCompressType::Oodle_Mermaid;
		else if (Value.Equals(TEXT("Oodle_Kraken"),    ESearchCase::IgnoreCase)) ExternalCompressTypeOverride = EPPatchExternalCompressType::Oodle_Kraken;
		else if (Value.Equals(TEXT("Oodle_Leviathan"), ESearchCase::IgnoreCase)) ExternalCompressTypeOverride = EPPatchExternalCompressType::Oodle_Leviathan;
		else
		{
			UE_LOG(LogPakPatcherCommandlet, Warning,
				TEXT("Unknown -ExternalCompressType value: %s. Allowed: None | Oodle_Selkie | Oodle_Mermaid | Oodle_Kraken | Oodle_Leviathan"),
				*Value);
		}
	}

	// -ExternalCompressLevel=0..9（int）
	{
		int32 LevelValue = 0;
		if (FParse::Value(*Params, TEXT("ExternalCompressLevel="), LevelValue))
		{
			ExternalCompressLevelOverride = LevelValue;
		}
	}

	// -CheckFileHashType=None|Crc32|MD5
	if (FParse::Value(*Params, TEXT("CheckFileHashType="), Value))
	{
		Value = Value.TrimStartAndEnd();
		if      (Value.Equals(TEXT("None"),  ESearchCase::IgnoreCase)) CheckFileHashTypeOverride = EPPakCheckFileHashType::None;
		else if (Value.Equals(TEXT("Crc32"), ESearchCase::IgnoreCase)) CheckFileHashTypeOverride = EPPakCheckFileHashType::Crc32;
		else if (Value.Equals(TEXT("MD5"),   ESearchCase::IgnoreCase)) CheckFileHashTypeOverride = EPPakCheckFileHashType::MD5;
		else
		{
			UE_LOG(LogPakPatcherCommandlet, Warning,
				TEXT("Unknown -CheckFileHashType value: %s. Allowed: None | Crc32 | MD5"), *Value);
		}
	}

	// HDiff bool 开关：-NoSingleCompress / -bUseBigCacheMatch / -bDoubleCheckEntry / -NoDoubleCheckEntry
	if (FParse::Param(*Params, TEXT("NoSingleCompress")))
	{
		UseSingleCompressModeOverride = false;
	}
	if (FParse::Param(*Params, TEXT("bUseBigCacheMatch")))
	{
		UseBigCacheMatchOverride = true;
	}
	if (FParse::Param(*Params, TEXT("bDoubleCheckEntry")))
	{
		DoubleCheckEntryOverride = true;
	}
	if (FParse::Param(*Params, TEXT("NoDoubleCheckEntry")))
	{
		DoubleCheckEntryOverride = false;
	}

	// HDiff int32 数值：-ThreadNum=, -StepMemSize=, -MinMatchScore=
	int32 IntValue = 0;
	if (FParse::Value(*Params, TEXT("ThreadNum="), IntValue))
	{
		ThreadNumOverride = IntValue;
	}
	if (FParse::Value(*Params, TEXT("StepMemSize="), IntValue))
	{
		StepMemSizeOverride = IntValue;
	}
	if (FParse::Value(*Params, TEXT("MinMatchScore="), IntValue))
	{
		MinMatchScoreOverride = IntValue;
	}
	if (FParse::Value(*Params, TEXT("PatchTaskThreadNum="), IntValue))
	{
		PatchTaskThreadNumOverride = IntValue;
	}
}

void FPPakPatcherCommandletParams::Print()
{
	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("CompressType: %s"), *UEnum::GetValueAsString(CompressType));
	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("PatchMode: %s"),    *UEnum::GetValueAsString(PatchMode));
	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("CheckMode: %s"),    *UEnum::GetValueAsString(CheckMode));

	LogOverrideOrIni(TEXT("CheckFileHashType"),     CheckFileHashTypeOverride,
		CheckFileHashTypeOverride.IsSet() ? CheckFileHashTypeToString(CheckFileHashTypeOverride.GetValue()) : FString());
	LogOverrideOrIni(TEXT("ExternalCompressType"),  ExternalCompressTypeOverride,
		ExternalCompressTypeOverride.IsSet() ? UEnum::GetValueAsString(ExternalCompressTypeOverride.GetValue()) : FString());
	LogOverrideOrIni(TEXT("ExternalCompressLevel"), ExternalCompressLevelOverride,
		ExternalCompressLevelOverride.IsSet() ? FString::FromInt(ExternalCompressLevelOverride.GetValue()) : FString());
	LogOverrideOrIni(TEXT("bUseSingleCompressMode"),UseSingleCompressModeOverride,
		UseSingleCompressModeOverride.IsSet() ? (UseSingleCompressModeOverride.GetValue() ? TEXT("true") : TEXT("false")) : FString());
	LogOverrideOrIni(TEXT("bUseBigCacheMatch"),     UseBigCacheMatchOverride,
		UseBigCacheMatchOverride.IsSet() ? (UseBigCacheMatchOverride.GetValue() ? TEXT("true") : TEXT("false")) : FString());
	LogOverrideOrIni(TEXT("bDoubleCheckEntry"),     DoubleCheckEntryOverride,
		DoubleCheckEntryOverride.IsSet() ? (DoubleCheckEntryOverride.GetValue() ? TEXT("true") : TEXT("false")) : FString());
	LogOverrideOrIni(TEXT("ThreadNum"),             ThreadNumOverride,
		ThreadNumOverride.IsSet() ? FString::Printf(TEXT("%d"), ThreadNumOverride.GetValue()) : FString());
	LogOverrideOrIni(TEXT("StepMemSize"),           StepMemSizeOverride,
		StepMemSizeOverride.IsSet() ? FString::Printf(TEXT("%d"), StepMemSizeOverride.GetValue()) : FString());
	LogOverrideOrIni(TEXT("MinMatchScore"),         MinMatchScoreOverride,
		MinMatchScoreOverride.IsSet() ? FString::Printf(TEXT("%d"), MinMatchScoreOverride.GetValue()) : FString());
	LogOverrideOrIni(TEXT("PatchTaskThreadNum"),    PatchTaskThreadNumOverride,
		PatchTaskThreadNumOverride.IsSet() ? FString::Printf(TEXT("%d"), PatchTaskThreadNumOverride.GetValue()) : FString());
}

void FPPakPatcherCommandletParams::ApplyOverridesToSettings() const
{
	UPPakPatcherSettings& S = UPPakPatcherSettings::Get();

	// PatchMode 始终写入（commandlet 命令行的 -Mode= 或 Params 默认值）
	S.PakPatchMode = PatchMode;

	if (CheckFileHashTypeOverride.IsSet())    S.CheckFileHashType      = CheckFileHashTypeOverride.GetValue();
	if (ExternalCompressTypeOverride.IsSet())  S.ExternalCompressType  = ExternalCompressTypeOverride.GetValue();
	if (ExternalCompressLevelOverride.IsSet()) S.ExternalCompressLevel = ExternalCompressLevelOverride.GetValue();
	if (UseSingleCompressModeOverride.IsSet())S.bUseSingleCompressMode = UseSingleCompressModeOverride.GetValue();
	if (UseBigCacheMatchOverride.IsSet())     S.bUseBigCacheMatch      = UseBigCacheMatchOverride.GetValue();
	if (DoubleCheckEntryOverride.IsSet())     S.bDoubleCheckEntry      = DoubleCheckEntryOverride.GetValue();
	if (ThreadNumOverride.IsSet())            S.ThreadNum              = ThreadNumOverride.GetValue();
	if (StepMemSizeOverride.IsSet())          S.PatchStepMemSize       = StepMemSizeOverride.GetValue();
	if (MinMatchScoreOverride.IsSet())        S.MinSingleMatchScore    = MinMatchScoreOverride.GetValue();
	if (PatchTaskThreadNumOverride.IsSet())   S.PatchTaskThreadNum     = PatchTaskThreadNumOverride.GetValue();

	// HDiff 配置类字段会被 BinPatcher 在构造时拷到自己的成员里；此时 BinPatcher 已经 StartupModule 完成，
	// 必须显式让 BinPatcher 重新拉取一次。
	if (UseSingleCompressModeOverride.IsSet() ||
		UseBigCacheMatchOverride.IsSet()      ||
		ThreadNumOverride.IsSet()             ||
		StepMemSizeOverride.IsSet()           ||
		MinMatchScoreOverride.IsSet())
	{
		if (IPBinPatcher* BinPatcher = IPPakPatcherModule::Get().GetBinPatcher())
		{
			BinPatcher->ReloadSettingsFromConfig();
		}
	}
}

void FPPakPatcherCommandletParams::WarnIrrelevantHDiffOverridesForApply(const TCHAR* InActionName) const
{
	// 这些 HDiff 参数仅 CreateDiff 时被 HDiff 算法使用；Apply 端透传到 Settings 是无害冗余，
	// 但容易让用户误以为可以在 Apply 端调优 patch 性能（会发邮件问"为什么 -ThreadNum=16 没生效"）。
	// 检测到时仅 warn，不阻断。
	// 注意：StepMemSize 不在此列表 —— 它在 Apply 端会从 patch 元数据读取，
	//      命令行透传相当于覆盖默认值，对没有该字段的旧 patch 仍生效；属于"半 Apply 端有意义"参数。
	if (ThreadNumOverride.IsSet())
	{
		UE_LOG(LogPakPatcherCommandlet, Warning,
			TEXT("[%s] -ThreadNum=%d is a CREATE-side HDiff param; ignored at Apply time (Apply uses patch metadata)."),
			InActionName, ThreadNumOverride.GetValue());
	}
	if (MinMatchScoreOverride.IsSet())
	{
		UE_LOG(LogPakPatcherCommandlet, Warning,
			TEXT("[%s] -MinMatchScore=%d is a CREATE-side HDiff param; ignored at Apply time."),
			InActionName, MinMatchScoreOverride.GetValue());
	}
	if (UseBigCacheMatchOverride.IsSet())
	{
		UE_LOG(LogPakPatcherCommandlet, Warning,
			TEXT("[%s] -bUseBigCacheMatch is a CREATE-side HDiff param; ignored at Apply time."),
			InActionName);
	}
	if (UseSingleCompressModeOverride.IsSet())
	{
		UE_LOG(LogPakPatcherCommandlet, Warning,
			TEXT("[%s] -NoSingleCompress is a CREATE-side HDiff param; the Apply-side mode is determined by the patch file metadata."),
			InActionName);
	}
}

int32 UPPakPatcherCommandlet::Main(const FString& Params)
{
	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("UPPakPatcherCommandlet params: %s"), *Params);

	Input.Parse(Params);
	Input.Print();

	// 把命令行覆盖统一应用到 UPPakPatcherSettings 单例（HDiff 配置同时刷到 BinPatcher）。
	// 未传的参数保持 ini 默认值不变；命令行覆盖只在当前 commandlet 进程内生效，不写回 ini。
	Input.ApplyOverridesToSettings();

	// 显式触发 KeyChain 加载并把结果应用到引擎全局（FCoreDelegates::GetRegisterEncryptionKeyMulticastDelegate），
	// 这样后续 FPakFile / FPakInfo 读取加密 pak 时也能拿到 key。
	// 命令行用法：
	//   -cryptokeys="<path/to/Crypto.json>"          // 推荐
	//   -aes="<32 字节 ANSI AES key>"                 // legacy
	//   -EncryptionKeyOverrideGuid=<guid>             // 多 key 时指定 PrincipalKey
	{
		FKeyChain& KeyChain = FPPakPatcherKeyChainHelper::Get().GetKeyChain(/*bForceReload*/ true);
		KeyChainUtilities::ApplyEncryptionKeys(KeyChain);
		const int32 NumEncKeys = KeyChain.GetEncryptionKeys().Num();
		const bool bHasSign = (KeyChain.GetSigningKey() != InvalidRSAKeyHandle);
		UE_LOG(LogPakPatcherCommandlet, Display, TEXT("KeyChain loaded. EncryptionKeys=%d, HasSigningKey=%s"),
			NumEncKeys, bHasSign ? TEXT("true") : TEXT("false"));
	}

	int32 ErrorCode = 0;

	// 注意：FParse::Param 是子串匹配，"CreatePakPatchWithDir" 会命中 "CreatePakPatch"。
	// 所以长的 action 名必须排在短的前面（先匹配）。
	if (FParse::Param(*Params, TEXT("CreatePakPatchWithDir")))
	{
		ErrorCode = CreatePakPatchWithDir(Params);
	}
	else if (FParse::Param(*Params, TEXT("PatchPakPatchWithDir")))
	{
		ErrorCode = PatchPakPatchWithDir(Params);
	}
	else if (FParse::Param(*Params, TEXT("CreatePakPatch")))
	{
		ErrorCode = CreatePakPatch(Params);
	}
	else if (FParse::Param(*Params, TEXT("CheckPakPatch")))
	{
		ErrorCode = CheckPakPatch(Params);
	}
	else if (FParse::Param(*Params, TEXT("PatchPak")))
	{
		ErrorCode = PatchPak(Params);
	}
	else if (FParse::Param(*Params, TEXT("SimpleTest")))
	{
		ErrorCode = SimpleTest(Params);
	}
	else if (FParse::Param(*Params, TEXT("UnitTest")))
	{
		ErrorCode = UnitTest(Params);
	}
	else
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("UPPakPatcherCommandlet - Unknown command."));
		return -1;
	}

	if (ErrorCode == 0)
	{
		UE_LOG(LogPakPatcherCommandlet, Display, TEXT("UPPakPatcherCommandlet - Successed."));
	}
	else
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("UPPakPatcherCommandlet - Failed. ErrorCode: %d"), ErrorCode);
		return ErrorCode;
	}

	return 0;
}

bool UPPakPatcherCommandlet::CheckFileParams(const TCHAR* Params, const TCHAR* Match, FString& OutParamValue, bool bCheckExist /*= false*/)
{
	if (!FParse::Value(Params, Match, OutParamValue))
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("PPakPatcher::CheckParams - '%s' must be set!"), Match);
		return false;
	}

	if (bCheckExist)
	{
		if (!IFileManager::Get().FileExists(*OutParamValue))
		{
			UE_LOG(LogPakPatcherCommandlet, Error, TEXT("PPakPatcher::CheckParams - File %s is not exist!"), *OutParamValue);
			return false;
		}
	}
	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("PPakPatcher::CheckParams - Successed. %s = %s"), Match, *OutParamValue);
	return true;
}

bool UPPakPatcherCommandlet::CheckDirParams(const TCHAR* Params, const TCHAR* Match, FString& OutParamValue, bool bCheckExist/* = false*/, bool bCreateIfNotExist/* = false*/)
{
	if (!FParse::Value(Params, Match, OutParamValue))
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("PPakPatcher::CheckParams - '%s' must be set!"), Match);
		return false;
	}

	bool bIsExist = IFileManager::Get().DirectoryExists(*OutParamValue);
	if (!bIsExist)
	{
		if (bCheckExist)
		{
			UE_LOG(LogPakPatcherCommandlet, Error, TEXT("PPakPatcher::CheckParams - Directory %s is not exist!"), *OutParamValue);
			return false;
		}
		if(bCreateIfNotExist)
		{
			if (!IFileManager::Get().MakeDirectory(*OutParamValue, true))
			{
				return false;
			}
		}
	}

	if (bCheckExist && !bIsExist)
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("PPakPatcher::CheckParams - Directory %s is not exist!"), *OutParamValue);
		return false;
	}

	if (!bIsExist)
	{
		if (IFileManager::Get().MakeDirectory(*OutParamValue, true))
		{
			return true;
		}
	}
	if (!IFileManager::Get().DirectoryExists(*OutParamValue))
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("PPakPatcher::CheckParams - Directory %s is not exist!"), *OutParamValue);
		return false;
	}

	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("PPakPatcher::CheckParams - Successed. %s = %s"), Match, *OutParamValue);
	return true;
}

int32 UPPakPatcherCommandlet::CreatePakPatch(const FString& Params)
{
	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Begin Create Pak Patch."));

	FString New;
	FString Old;
	FString Patch;

	// 统一使用 NewPak/OldPak 命名（与 -CheckPakPatch / -PatchPak 风格一致）
	if (!FParse::Value(*Params, TEXT("NewPak="), New, true))
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("PPakPatcher::CheckParams - '%s' must be set!"), TEXT("NewPak"));
		return -1;
	}
	if (!FParse::Value(*Params, TEXT("OldPak="), Old, true))
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("PPakPatcher::CheckParams - '%s' must be set!"), TEXT("OldPak"));
		return -1;
	}
	if (!FParse::Value(*Params, TEXT("Patch="), Patch, false))
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("PPakPatcher::CheckParams - '%s' must be set!"), TEXT("Patch"));
		return -1;
	}

	if (IFileManager::Get().FileExists(*New))
	{
		// single file mode.
		UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Create Pak Patch with SingleFileMode. "));

		if (!CreatePakPatch_Internal(New, Old, Patch))
		{
			return -1;
		}
	}
	else if (IFileManager::Get().DirectoryExists(*New))
	{
		// directory mode.
		UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Create Pak Patch with DirectoryMode. Mode=%s"),
			*UEnum::GetValueAsString(Input.PatchMode));

		TArray<FPFileCompareInfo> FileCompareInfos = FPPakPatcherUtils::CompareDirectories(New, Old);
		for (FPFileCompareInfo& Info : FileCompareInfos)
		{
			UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Filename:%s, DiffType: %s"), *Info.Filename, *UEnum::GetValueAsString(Info.DiffType));

			if (Info.DiffType == EPFileCompareDiffType::Equal)
			{
				UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Skip equal file. Pak: %s"), *Info.Filename);
			}
			else if (Info.DiffType == EPFileCompareDiffType::Add)
			{
				const FString CopyTarget = FPaths::Combine(Patch, Info.Filename);
				UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Copy Pak file by Add - from: %s, to: %s"), *Info.NewFullPath, *CopyTarget);
				if (COPY_OK != IFileManager::Get().Copy(*CopyTarget, *Info.NewFullPath))
				{
					UE_LOG(LogPakPatcherCommandlet, Error, TEXT("CopyFromContent: Failed copy from [%s] to [%s] !"), *Info.NewFullPath, *CopyTarget);
					return -1;
				}
			}
			else if (Info.DiffType == EPFileCompareDiffType::Delete)
			{
				// TODO: record delete case
				UE_LOG(LogPakPatcherCommandlet, Display, TEXT("skip delete file - Pak: %s"), *Info.Filename);
			}
			else if (Info.DiffType == EPFileCompareDiffType::Modify)
			{
				UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Create Pak Patch by Modify - New: %s, Old: %s"), *Info.NewFullPath, *Info.OldFullPath);
				const FString& PatchFile = FPaths::Combine(Patch, Info.Filename);
				bool bOk = CreatePakPatch_Internal(Info.NewFullPath, Info.OldFullPath, PatchFile);
				if (!bOk)
				{
					UE_LOG(LogPakPatcherCommandlet, Error, TEXT("Failed to genrate patch from new [%s] and old [%s] !"), *Info.NewFullPath, *Info.OldFullPath);
					return -1;
				}
			}
			else
			{
				UE_LOG(LogPakPatcherCommandlet, Error, TEXT("Unknown DiffType: %s"), *UEnum::GetValueAsString(Info.DiffType));
				return -1;
			}
		}
	}
	else
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("PPakPatcher::CheckParams - '%s' is not exist!"), *New);
		return -1;
	}

	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Finish Create Pak Patch."));
	return 0;
}


bool UPPakPatcherCommandlet::CreatePakPatch_Internal(const FString& InNewPakFilename, const FString& InOldPakFilename, const FString& InPatchFilename)
{
	FPResPatcher ResPatcher;
	FPResPatchDataPtr PatchData;
	if (!ResPatcher.CreateDiff(InPatchFilename, InNewPakFilename, InOldPakFilename, PatchData, Input.PatchMode, Input.CompressType))
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("CreateDiff Failed. %s"), *InPatchFilename);
		return false;
	}
	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("CreateDiff Successed. %s"), *InPatchFilename);

	// save patch data to file.
	if (PatchData.IsValid() && PatchData->IsUsePrecache())
	{
		if (PatchData->SaveToFile(InPatchFilename))
		{
			UE_LOG(LogPakPatcherCommandlet, Display, TEXT("PatchData SaveToFile Successed. %s"), *InPatchFilename);
		}
		else
		{
			UE_LOG(LogPakPatcherCommandlet, Error, TEXT("PatchData SaveToFile Failed. %s"), *InPatchFilename);
			return false;
		}
	}

	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Finish CreatePakPatch process. %s"), *InPatchFilename);
	return true;
}

int32 UPPakPatcherCommandlet::CheckPakPatch(const FString& Params)
{
	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Begin Check Pak Patch."));
	Input.WarnIrrelevantHDiffOverridesForApply(TEXT("CheckPakPatch"));

	FString NewPakFilename;
	FString OldPakFilename;
	FString PatchFileName;

	if (!CheckFileParams(*Params, TEXT("NewPak="), NewPakFilename, true))
	{
		return -1;
	}

	if (!CheckFileParams(*Params, TEXT("OldPak="), OldPakFilename, true))
	{
		return -1;
	}
	if (!CheckFileParams(*Params, TEXT("Patch="), PatchFileName, true))
	{
		return -1;
	}

	// load patch data.
	FPResPatchDataPtr PatchData = MakeShareable(new FPResPatchData());
	if (PatchData->LoadFromFile(PatchFileName))
	{
		UE_LOG(LogPakPatcherCommandlet, Display, TEXT("LoadFromFile Success."));
	}
	else
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("LoadFromFile Failed."));
		return -1;
	}

	FPResPatcher ResPatcher;
	if (ResPatcher.CheckDiff(NewPakFilename, OldPakFilename, PatchData))
	{
		UE_LOG(LogPakPatcherCommandlet, Display, TEXT("CheckDiff Successed. %s"), *PatchFileName);
	}
	else
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("CheckDiff Failed. %s"), *PatchFileName);
		return -1;
	}
	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Finish CheckPakPatch process. %s"), *PatchFileName);

	return 0;
}

int32 UPPakPatcherCommandlet::PatchPak(const FString& Params)
{
	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Begin Patch Pak."));
	Input.WarnIrrelevantHDiffOverridesForApply(TEXT("PatchPak"));


	FString NewPakFilename;
	FString OldPakFilename;
	FString PatchFileName;


	if (!CheckFileParams(*Params, TEXT("NewPak="), NewPakFilename, false))
	{
		return -1;
	}
	if (!CheckFileParams(*Params, TEXT("OldPak="), OldPakFilename, true))
	{
		return -1;
	}
	if (!CheckFileParams(*Params, TEXT("Patch="), PatchFileName, true))
	{
		return -1;
	}

	// load patch data.
	FPResPatchDataPtr PatchData = MakeShareable(new FPResPatchData());
	if (PatchData->LoadFromFile(PatchFileName))
	{
		UE_LOG(LogPakPatcherCommandlet, Display, TEXT("LoadFromFile Success."));
	}
	else
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("LoadFromFile Failed."));
		return -1;
	}

	FPResPatcher ResPatcher;
	if (ResPatcher.PatchDiff(NewPakFilename, OldPakFilename, PatchData))
	{
		UE_LOG(LogPakPatcherCommandlet, Display, TEXT("PatchDiff Successed. %s"), *PatchFileName);
	}
	else
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("PatchDiff Failed. %s"), *PatchFileName);
		return -1;
	}
	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Finish PatchPak process. %s"), *PatchFileName);

	return 0;
}

TArray<FString> UPPakPatcherCommandlet::GatherPaksInDirectory(const FString InDir)
{
	// 已弃用：现统一由 FPPatchManager 内部按 chunk 名匹配。保留空实现避免外部链接错误。
	return TArray<FString>();
}

TMap<FString, FString> UPPakPatcherCommandlet::MakeNewOldMatchMap(const FString& InNewDir, const FString& InOldDir)
{
	// 已弃用：现统一由 FPPatchManager::CreatePatch 内部按 chunk 名匹配。
	return TMap<FString, FString>();
}

int32 UPPakPatcherCommandlet::CreatePakPatchWithDir(const FString& Params)
{
	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Begin Patch Pak."));

	FString NewDir;
	FString OldDir;
	FString PatchDir;

	if (!CheckDirParams(*Params, TEXT("NewDir="), NewDir, true))
	{
		return -1;
	}
	if (!CheckDirParams(*Params, TEXT("OldDir="), OldDir, true))
	{
		return -1;
	}
	if (!CheckDirParams(*Params, TEXT("PatchDir="), PatchDir, false, true))
	{
		return -1;
	}

	// 走统一入口 FPPatchManager::CreatePatch：
	//   - 自动读 <NewDir>/md5_file_list.txt 与 <OldDir>/md5_file_list.txt
	//   - 自动按 chunk 名匹配新旧 .pak（含 IoStore 同伴文件联动）
	//   - 自动按 New/Old MD5 判定 DiffType（Equal/Modify/Add/Delete）
	//   - 自动产出 patch_manifest.txt
	if (!FPPatchManager::Get().CreatePatch(OldDir, NewDir, PatchDir, Input.CompressType))
	{
		UE_LOG(LogPakPatcherCommandlet, Error,
			TEXT("CreatePakPatchWithDir failed. New:%s Old:%s Patch:%s"), *NewDir, *OldDir, *PatchDir);
		return -1;
	}

	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Finish generate Patch files. output dir:%s"), *PatchDir);
	return 0;
}

// =========================================================================
// PatchPakPatchWithDir
// =========================================================================

int32 UPPakPatcherCommandlet::PatchPakPatchWithDir(const FString& Params)
{
	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Begin PatchPakPatchWithDir."));
	Input.WarnIrrelevantHDiffOverridesForApply(TEXT("PatchPakPatchWithDir"));

	FString ResDir;
	FString PatchDir;

	if (!CheckDirParams(*Params, TEXT("ResDir="), ResDir, true))
	{
		return -1;
	}
	if (!CheckDirParams(*Params, TEXT("PatchDir="), PatchDir, true))
	{
		return -1;
	}

	const bool bSkipVerifyBefore = Params.Contains(TEXT("-NoVerifyBefore"));
	const bool bSkipVerifyAfter  = Params.Contains(TEXT("-NoVerifyAfter"));

	// --- Step 1: VerifyBeforePatch ---
	if (!bSkipVerifyBefore)
	{
		UE_LOG(LogPakPatcherCommandlet, Display, TEXT("[PatchPakPatchWithDir] Step 1/3: VerifyBeforePatch..."));
		const double T0 = FPlatformTime::Seconds();

		// 构建 CRC 表：从 ResDir/md5_file_list.txt 读取每个文件的 CRC
		FPUpdateManifestSummary ResSummary;
		if (!ResSummary.Load(ResDir / FPPatchManager::GetSourceManifestFileName()))
		{
			UE_LOG(LogPakPatcherCommandlet, Error, TEXT("[PatchPakPatchWithDir] Failed to load ResDir manifest."));
			return -1;
		}
		TMap<FString, uint32> CrcMap;
		for (const auto& KV : ResSummary.GetManifestFileItems())
		{
			CrcMap.Add(KV.Key, KV.Value.CRC);
		}

		if (!FPPatchManager::Get().VerifyBeforePatch(PatchDir, CrcMap, /*bAllowMissing*/ true))
		{
			UE_LOG(LogPakPatcherCommandlet, Error, TEXT("[PatchPakPatchWithDir] VerifyBeforePatch FAILED."));
			return -1;
		}
		UE_LOG(LogPakPatcherCommandlet, Display, TEXT("[PatchPakPatchWithDir] VerifyBeforePatch passed. (%.2fs)"),
			FPlatformTime::Seconds() - T0);
	}
	else
	{
		UE_LOG(LogPakPatcherCommandlet, Display, TEXT("[PatchPakPatchWithDir] Step 1/3: VerifyBeforePatch SKIPPED (-NoVerifyBefore)."));
	}

	// --- Step 2: ApplyPatch ---
	{
		UE_LOG(LogPakPatcherCommandlet, Display, TEXT("[PatchPakPatchWithDir] Step 2/3: ApplyPatch..."));
		const double T0 = FPlatformTime::Seconds();
		if (!FPPatchManager::Get().ApplyPatch(ResDir, PatchDir))
		{
			UE_LOG(LogPakPatcherCommandlet, Error, TEXT("[PatchPakPatchWithDir] ApplyPatch FAILED."));
			return -1;
		}
		UE_LOG(LogPakPatcherCommandlet, Display, TEXT("[PatchPakPatchWithDir] ApplyPatch succeeded. (%.2fs)"),
			FPlatformTime::Seconds() - T0);
	}

	// --- Step 3: VerifyAfterPatch ---
	if (!bSkipVerifyAfter)
	{
		UE_LOG(LogPakPatcherCommandlet, Display, TEXT("[PatchPakPatchWithDir] Step 3/3: VerifyAfterPatch..."));
		const double T0 = FPlatformTime::Seconds();

		// 打补丁后重新算 CRC：扫描 ResDir 下所有文件
		TMap<FString, uint32> NewCrcMap;
		TArray<FString> ResFiles;
		IFileManager::Get().FindFilesRecursive(ResFiles, *ResDir, TEXT("*.*"), /*Files*/true, /*Dirs*/false);
		for (const FString& FullPath : ResFiles)
		{
			const FString FileName = FPaths::GetCleanFilename(FullPath);
			NewCrcMap.Add(FileName, FPPakPatcherUtils::CalculateFileCrc32(FullPath));
		}

		if (!FPPatchManager::Get().VerifyAfterPatch(PatchDir, NewCrcMap, /*bAllowMissing*/ true))
		{
			UE_LOG(LogPakPatcherCommandlet, Error, TEXT("[PatchPakPatchWithDir] VerifyAfterPatch FAILED."));
			return -1;
		}
		UE_LOG(LogPakPatcherCommandlet, Display, TEXT("[PatchPakPatchWithDir] VerifyAfterPatch passed. (%.2fs)"),
			FPlatformTime::Seconds() - T0);
	}
	else
	{
		UE_LOG(LogPakPatcherCommandlet, Display, TEXT("[PatchPakPatchWithDir] Step 3/3: VerifyAfterPatch SKIPPED (-NoVerifyAfter)."));
	}

	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Finish PatchPakPatchWithDir. ResDir:%s"), *ResDir);
	return 0;
}

int32 UPPakPatcherCommandlet::SimpleTest(const FString& Params)
{
	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Begin SimpleTest."));
	if (FPPakPatcherUnitTest::Get().SimpleTest())
	{
        UE_LOG(LogPakPatcherCommandlet, Display, TEXT("SimpleTest Successed."));
	}
	else
	{
        UE_LOG(LogPakPatcherCommandlet, Error, TEXT("SimpleTest Failed."));
		return -1;
	}
	return 0;
}

int32 UPPakPatcherCommandlet::UnitTest(const FString& Params)
{
	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Begin UnitTest."));

	// CheckMode / Mode / Compress 等参数均来自成员 Input（已在 Main() 中解析）。
	const EPPakUnitTestCheckMode CheckMode = Input.CheckMode;
	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("UnitTest CheckMode=%s"),
		CheckMode == EPPakUnitTestCheckMode::PatchAndCompare ? TEXT("PatchAndCompare") :
		CheckMode == EPPakUnitTestCheckMode::HDiffCheck      ? TEXT("HDiffCheck")      : TEXT("Both"));

	FString NewDir;
	FString OldDir;
	FString Output;

	if (!CheckDirParams(*Params, TEXT("NewDir="), NewDir, true))
	{
		return -1;
	}
	if (!CheckDirParams(*Params, TEXT("OldDir="), OldDir, true))
	{
		return -1;
	}
	if (!CheckDirParams(*Params, TEXT("Output="), Output, false, true))
	{
		return -1;
	}

	const FString PatchDir = Output / TEXT("Patch");
	const FString PatchedNew = Output / TEXT("PatchedNew");

	// 回测 1：CreateDiff -> PatchPak -> 对比 MD5（已由 UnitTest 类闭环实现）
	const bool bNeedPatchAndCompare =
		(CheckMode == EPPakUnitTestCheckMode::PatchAndCompare) || (CheckMode == EPPakUnitTestCheckMode::Both);
	if (bNeedPatchAndCompare)
	{
		if (FPPakPatcherUnitTest::Get().DirecotryDiffPatchTest(NewDir, OldDir, Output, PatchDir, PatchedNew))
		{
			UE_LOG(LogPakPatcherCommandlet, Display, TEXT("UnitTest [PatchAndCompare] Successed."));
		}
		else
		{
			UE_LOG(LogPakPatcherCommandlet, Error, TEXT("UnitTest [PatchAndCompare] Failed."));
			return -1;
		}
	}

	// 回测 2：HDiff CheckDiff 接口 —— 对 PatchDir 下每个 .patch 调一次 FPResPatcher::CheckDiff
	const bool bNeedHDiffCheck =
		(CheckMode == EPPakUnitTestCheckMode::HDiffCheck) || (CheckMode == EPPakUnitTestCheckMode::Both);
	if (bNeedHDiffCheck)
	{
		FPResPatcher ResPatcher;
		TArray<FString> PatchFiles;
		IFileManager::Get().FindFilesRecursive(PatchFiles, *PatchDir, TEXT("*.patch"), true, false);
		int32 CheckedCount = 0;
		for (const FString& PatchFile : PatchFiles)
		{
			const FString RelPath = PatchFile.RightChop(PatchDir.Len()).TrimChar(TEXT('/')).TrimChar(TEXT('\\'));
			FString RelNoExt = RelPath;
			RelNoExt.RemoveFromEnd(TEXT(".patch"));
			const FString NewPakFile = NewDir / RelNoExt;
			const FString OldPakFile = OldDir / RelNoExt;

			if (!IFileManager::Get().FileExists(*NewPakFile) || !IFileManager::Get().FileExists(*OldPakFile))
			{
				UE_LOG(LogPakPatcherCommandlet, Warning, TEXT("UnitTest [HDiffCheck] skip: missing new/old. patch=%s"), *PatchFile);
				continue;
			}

			FPResPatchDataPtr Patch = MakeShareable(new FPResPatchData());
			if (!Patch->LoadFromFile(PatchFile))
			{
				UE_LOG(LogPakPatcherCommandlet, Error, TEXT("UnitTest [HDiffCheck] load patch failed for %s"), *PatchFile);
				return -1;
			}
			if (!ResPatcher.CheckDiff(NewPakFile, OldPakFile, Patch))
			{
				UE_LOG(LogPakPatcherCommandlet, Error, TEXT("UnitTest [HDiffCheck] CheckDiff failed for %s"), *PatchFile);
				return -1;
			}
			++CheckedCount;
		}
		UE_LOG(LogPakPatcherCommandlet, Display, TEXT("UnitTest [HDiffCheck] Successed. Checked=%d"), CheckedCount);
	}

	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Run All Tests Successed."));
	return 0;
}


