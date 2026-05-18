#include "Data/PPakPatcherKeyChainHelper.h"
#include "Data/PPakPatcherDataType.h"

#include "Misc/LazySingleton.h"
#include "Misc/IEngineCrypto.h"
#include "Misc/KeyChainUtilities.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Base64.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "RSA.h"

#include "HAL/FileManager.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FPPakPatcherKeyChainHelper& FPPakPatcherKeyChainHelper::Get()
{
	return TLazySingleton<FPPakPatcherKeyChainHelper>::Get();
}

FKeyChain& FPPakPatcherKeyChainHelper::GetKeyChain(bool bForceReload)
{
	LoadKeyChain(bForceReload);
	return KeyChain;
}


bool FPPakPatcherKeyChainHelper::Signed()
{
	//Signed if we have keys, and are not running with fileopenlog (currently results in a deadlock).
	return KeyChain.GetSigningKey() != InvalidRSAKeyHandle && !FParse::Param(FCommandLine::Get(), TEXT("fileopenlog"));
}

bool FPPakPatcherKeyChainHelper::HasEncryptionKey()
{
	return KeyChain.GetEncryptionKeys().Num() > 0;
}


void FPPakPatcherKeyChainHelper::LoadKeyChain(bool bForceReload)
{
	if (bLoadOnce || bForceReload)
	{
		bLoadOnce = false;
		KeyChain = FKeyChain();
		
#if WITH_EDITOR || IS_PROGRAM
		LoadKeyChainInEditor();
#else
		LoadKeyChainInGame();
#endif	
	}
}

void FPPakPatcherKeyChainHelper::LoadKeyChainInEditor()
{
	bool bSuccess = false;

	if (!bSuccess)
	{
		bSuccess = LoadKeyChainFromCommandline();
	}

	if (!bSuccess)
	{
		bSuccess = LoadKeyChainFromFile();
	}

	if (!bSuccess)
	{
		bSuccess = LoadKeyChainFromEngineIni();
	}

	if (bSuccess)
	{
		// override encryption key guid.
		const TCHAR* CmdLine = FCommandLine::Get();
		FString EncryptionKeyOverrideGuidString;
		FGuid EncryptionKeyOverrideGuid;
		if (FParse::Value(CmdLine, TEXT("EncryptionKeyOverrideGuid="), EncryptionKeyOverrideGuidString))
		{
			FGuid::Parse(EncryptionKeyOverrideGuidString, EncryptionKeyOverrideGuid);
		}
		KeyChain.SetPrincipalEncryptionKey(KeyChain.GetEncryptionKeys().Find(EncryptionKeyOverrideGuid));
	}
}


bool FPPakPatcherKeyChainHelper::LoadKeyChainFromFile()
{
	// 编辑器/Commandlet 模式下，可以通过 Crypto.json 来获取。构建后会在: Saved/Cooked/[Platform]/[Game]/Metadata/Crypto.json
	// 命令行用法（参考引擎）：
	//   -cryptokeys="<path/to/Crypto.json>"
	const TCHAR* CmdLine = FCommandLine::Get();
	FString CryptoKeysCacheFilename;
	if (!FParse::Value(CmdLine, TEXT("cryptokeys="), CryptoKeysCacheFilename))
	{
		return false;
	}
	if (CryptoKeysCacheFilename.IsEmpty())
	{
		return false;
	}

	// 路径标准化：相对路径相对于项目根（FPaths::ProjectDir）解析
	if (FPaths::IsRelative(CryptoKeysCacheFilename))
	{
		CryptoKeysCacheFilename = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), CryptoKeysCacheFilename);
	}
	FPaths::MakeStandardFilename(CryptoKeysCacheFilename);

	if (!IFileManager::Get().FileExists(*CryptoKeysCacheFilename))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcherKeyChainHelper::LoadKeyChainFromFile - cryptokeys file not found: %s"),
			*CryptoKeysCacheFilename);
		return false;
	}

	UE_LOG(LogPPakPacher, Display, TEXT("FPPakPatcherKeyChainHelper::LoadKeyChainFromFile - Parsing crypto keys from: %s"),
		*CryptoKeysCacheFilename);
	KeyChainUtilities::LoadKeyChainFromFile(CryptoKeysCacheFilename, KeyChain);

	const int32 NumEncKeys = KeyChain.GetEncryptionKeys().Num();
	const bool bHasSign = (KeyChain.GetSigningKey() != InvalidRSAKeyHandle);
	UE_LOG(LogPPakPacher, Display, TEXT("FPPakPatcherKeyChainHelper::LoadKeyChainFromFile - Loaded. EncryptionKeys=%d, HasSigningKey=%s"),
		NumEncKeys, bHasSign ? TEXT("true") : TEXT("false"));
	return NumEncKeys > 0 || bHasSign;
}


bool FPPakPatcherKeyChainHelper::LoadKeyChainFromEngineIni()
{
	const TCHAR* CmdLine = FCommandLine::Get();

	FString EngineIni;


	FString ProjectDir = FPaths::ProjectDir();
	FString EngineDir = FPaths::EngineDir();
	FString Platform = FPaths::ProjectDir();

	if (FParse::Value(CmdLine, TEXT("projectdir="), ProjectDir, false)
		&& FParse::Value(CmdLine, TEXT("enginedir="), EngineDir, false)
		&& FParse::Value(CmdLine, TEXT("platform="), Platform, false))
	{
		UE_LOG(LogPPakPacher, Warning, TEXT("A legacy command line syntax is being used for crypto config. Please update to using the -cryptokey parameter as soon as possible as this mode is deprecated"));

		FConfigFile EngineConfig;

		FConfigCacheIni::LoadExternalIniFile(EngineConfig, TEXT("Engine"), *FPaths::Combine(EngineDir, TEXT("Config\\")), *FPaths::Combine(ProjectDir, TEXT("Config/")), true, *Platform);
		bool bDataCryptoRequired = false;
		EngineConfig.GetBool(TEXT("PlatformCrypto"), TEXT("PlatformRequiresDataCrypto"), bDataCryptoRequired);

		if (!bDataCryptoRequired)
		{
			return false;
		}

		FConfigFile ConfigFile;
		FConfigCacheIni::LoadExternalIniFile(ConfigFile, TEXT("Crypto"), *FPaths::Combine(EngineDir, TEXT("Config\\")), *FPaths::Combine(ProjectDir, TEXT("Config/")), true, *Platform);
		bool bSignPak = false;
		bool bEncryptPakIniFiles = false;
		bool bEncryptPakIndex = false;
		bool bEncryptAssets = false;
		bool bEncryptPak = false;

		if (ConfigFile.Num())
		{
			UE_LOG(LogPPakPacher, Display, TEXT("Using new format crypto.ini files for crypto configuration"));

			static const TCHAR* SectionName = TEXT("/Script/CryptoKeys.CryptoKeysSettings");

			ConfigFile.GetBool(SectionName, TEXT("bEnablePakSigning"), bSignPak);
			ConfigFile.GetBool(SectionName, TEXT("bEncryptPakIniFiles"), bEncryptPakIniFiles);
			ConfigFile.GetBool(SectionName, TEXT("bEncryptPakIndex"), bEncryptPakIndex);
			ConfigFile.GetBool(SectionName, TEXT("bEncryptAssets"), bEncryptAssets);
			bEncryptPak = bEncryptPakIniFiles || bEncryptPakIndex || bEncryptAssets;

			if (bSignPak)
			{
				FString PublicExpBase64, PrivateExpBase64, ModulusBase64;
				ConfigFile.GetString(SectionName, TEXT("SigningPublicExponent"), PublicExpBase64);
				ConfigFile.GetString(SectionName, TEXT("SigningPrivateExponent"), PrivateExpBase64);
				ConfigFile.GetString(SectionName, TEXT("SigningModulus"), ModulusBase64);

				TArray<uint8> PublicExp, PrivateExp, Modulus;
				FBase64::Decode(PublicExpBase64, PublicExp);
				FBase64::Decode(PrivateExpBase64, PrivateExp);
				FBase64::Decode(ModulusBase64, Modulus);

				KeyChain.SetSigningKey(FRSA::CreateKey(PublicExp, PrivateExp, Modulus));

				UE_LOG(LogPPakPacher, Display, TEXT("Parsed signature keys from config files."));

			}

			if (bEncryptPak)
			{
				FString EncryptionKeyString;
				ConfigFile.GetString(SectionName, TEXT("EncryptionKey"), EncryptionKeyString);

				if (EncryptionKeyString.Len() > 0)
				{
					TArray<uint8> Key;
					FBase64::Decode(EncryptionKeyString, Key);
					check(Key.Num() == sizeof(FAES::FAESKey::Key));
					FNamedAESKey NewKey;
					NewKey.Name = TEXT("Default");
					NewKey.Guid = FGuid();
					FMemory::Memcpy(NewKey.Key.Key, &Key[0], sizeof(FAES::FAESKey::Key));
					KeyChain.GetEncryptionKeys().Add(NewKey.Guid, NewKey);
					UE_LOG(LogPPakPacher, Display, TEXT("Parsed AES encryption key from config files."));
				}

			}
		}
		else
		{
			static const TCHAR* SectionName = TEXT("Core.Encryption");

			UE_LOG(LogPPakPacher, Display, TEXT("Using old format encryption.ini files for crypto configuration"));

			FConfigCacheIni::LoadExternalIniFile(ConfigFile, TEXT("Encryption"), *FPaths::Combine(EngineDir, TEXT("Config\\")), *FPaths::Combine(ProjectDir, TEXT("Config/")), true, *Platform);
			ConfigFile.GetBool(SectionName, TEXT("SignPak"), bSignPak);
			ConfigFile.GetBool(SectionName, TEXT("EncryptPak"), bEncryptPak);

			if (bSignPak)
			{
				FString RSAPublicExp, RSAPrivateExp, RSAModulus;
				ConfigFile.GetString(SectionName, TEXT("rsa.publicexp"), RSAPublicExp);
				ConfigFile.GetString(SectionName, TEXT("rsa.privateexp"), RSAPrivateExp);
				ConfigFile.GetString(SectionName, TEXT("rsa.modulus"), RSAModulus);

				//TODO: Fix me!
				//OutSigningKey.PrivateKey.Exponent.Parse(RSAPrivateExp);
				//OutSigningKey.PrivateKey.Modulus.Parse(RSAModulus);
				//OutSigningKey.PublicKey.Exponent.Parse(RSAPublicExp);
				//OutSigningKey.PublicKey.Modulus = OutSigningKey.PrivateKey.Modulus;

				UE_LOG(LogPPakPacher, Display, TEXT("Parsed signature keys from config files."));
			}

			if (bEncryptPak)
			{
				FString EncryptionKeyString;
				ConfigFile.GetString(SectionName, TEXT("aes.key"), EncryptionKeyString);
				FNamedAESKey NewKey;
				NewKey.Name = TEXT("Default");
				NewKey.Guid = FGuid();
				if (EncryptionKeyString.Len() == 32 && TCString<TCHAR>::IsPureAnsi(*EncryptionKeyString))
				{
					for (int32 Index = 0; Index < 32; ++Index)
					{
						NewKey.Key.Key[Index] = (uint8)EncryptionKeyString[Index];
					}
					KeyChain.GetEncryptionKeys().Add(NewKey.Guid, NewKey);
					UE_LOG(LogPPakPacher, Display, TEXT("Parsed AES encryption key from config files."));
				}
			}

		}
	}
	return false;
}


bool FPPakPatcherKeyChainHelper::LoadKeyChainFromCommandline()
{
	const TCHAR* CmdLine = FCommandLine::Get();

	UE_LOG(LogPPakPacher, Display, TEXT("Using command line for crypto configuration"));

	bool bAnyLoaded = false;

	// -------------------------------------------------------------
	// -SignKey="<path/to/SignKey.json>"
	//   独立的 RSA 签名 key JSON 文件（与 -cryptokeys 解耦，便于只签名场景）。
	//   JSON 形如：
	//     {
	//       "PublicKey":  { "Exponent": "<base64>", "Modulus": "<base64>" },
	//       "PrivateKey": { "Exponent": "<base64>", "Modulus": "<base64>" }
	//     }
	// -------------------------------------------------------------
	{
		FString SignKeyFile;
		if (FParse::Value(CmdLine, TEXT("SignKey="), SignKeyFile) && !SignKeyFile.IsEmpty())
		{
			if (FPaths::IsRelative(SignKeyFile))
			{
				SignKeyFile = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), SignKeyFile);
			}
			FPaths::MakeStandardFilename(SignKeyFile);

			if (!IFileManager::Get().FileExists(*SignKeyFile))
			{
				UE_LOG(LogPPakPacher, Error,
					TEXT("FPPakPatcherKeyChainHelper::LoadKeyChainFromCommandline - -SignKey file not found: %s"),
					*SignKeyFile);
			}
			else
			{
				TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*SignKeyFile));
				if (File.IsValid())
				{
					TSharedPtr<FJsonObject> RootObject;
					TSharedRef<TJsonReader<UTF8CHAR>> Reader = TJsonReaderFactory<UTF8CHAR>::Create(File.Get());
					if (FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid())
					{
						FRSAKeyHandle SigningKey = KeyChainUtilities::ParseRSAKeyFromJson(RootObject);
						if (SigningKey != InvalidRSAKeyHandle)
						{
							KeyChain.SetSigningKey(SigningKey);
							UE_LOG(LogPPakPacher, Display,
								TEXT("Parsed RSA signing key from -SignKey: %s"), *SignKeyFile);
							bAnyLoaded = true;
						}
						else
						{
							UE_LOG(LogPPakPacher, Error,
								TEXT("FPPakPatcherKeyChainHelper::LoadKeyChainFromCommandline - Failed to parse RSA key from -SignKey: %s. ")
								TEXT("Expected JSON: { 'PublicKey': {'Exponent','Modulus'}, 'PrivateKey': {'Exponent','Modulus'} } (all base64)."),
								*SignKeyFile);
						}
					}
					else
					{
						UE_LOG(LogPPakPacher, Error,
							TEXT("FPPakPatcherKeyChainHelper::LoadKeyChainFromCommandline - Failed to deserialize JSON: %s"),
							*SignKeyFile);
					}
				}
			}
		}
	}

	// -------------------------------------------------------------
	// -aes="<32 bytes ANSI>"  legacy AES key
	// -------------------------------------------------------------
	FString EncryptionKeyString;
	FParse::Value(CmdLine, TEXT("aes="), EncryptionKeyString, false);

	if (EncryptionKeyString.Len() > 0)
	{
		UE_LOG(LogPPakPacher, Warning, TEXT("A legacy command line syntax is being used for crypto config. Please update to using the -cryptokey parameter as soon as possible as this mode is deprecated"));

		FNamedAESKey NewKey;
		NewKey.Name = TEXT("Default");
		NewKey.Guid = FGuid();
		const uint32 RequiredKeyLength = sizeof(NewKey.Key);

		// Error checking
		if (EncryptionKeyString.Len() < RequiredKeyLength)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("AES encryption key must be %d characters long"), RequiredKeyLength);
			return bAnyLoaded;
		}

		if (EncryptionKeyString.Len() > RequiredKeyLength)
		{
			UE_LOG(LogPPakPacher, Warning, TEXT("AES encryption key is more than %d characters long, so will be truncated!"), RequiredKeyLength);
			EncryptionKeyString.LeftInline(RequiredKeyLength);
		}

		if (!FCString::IsPureAnsi(*EncryptionKeyString))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("AES encryption key must be a pure ANSI string!"));
			return bAnyLoaded;
		}

		auto AnsiKey = StringCast<ANSICHAR>(*EncryptionKeyString);
		const ANSICHAR* AsAnsi = AnsiKey.Get();
		check(TCString<ANSICHAR>::Strlen(AsAnsi) == RequiredKeyLength);
		FMemory::Memcpy(NewKey.Key.Key, AsAnsi, RequiredKeyLength);

		KeyChain.GetEncryptionKeys().Add(NewKey.Guid, NewKey);
		UE_LOG(LogPPakPacher, Display, TEXT("Parsed AES encryption key from command line."));
		return true;
	}

	return bAnyLoaded;
}

void FPPakPatcherKeyChainHelper::LoadKeyChainInGame()
{
	// 当Config路径内存在DefaultCrypto.ini时，构建中会用UE_REGISTER_SIGNING_KEY和UE_REGISTER_ENCRYPTION_KEY将签名钥匙和加密钥匙写入到代码中。
	// 可以通过FCoreDelegates::GetPakSigningKeysDelegate()和FCoreDelegates::GetPakEncryptionKeyDelegate()来获取这些key

	// sign key
	if (FCoreDelegates::GetPakSigningKeysDelegate().IsBound())
	{
		TArray<uint8> Exponent;
		TArray<uint8> Modulus;
		FCoreDelegates::GetPakSigningKeysDelegate().Execute(Exponent, Modulus);
		KeyChain.SetSigningKey(FRSA::CreateKey(Exponent, TArray<uint8>(), Modulus));
	}

	// encrypt key
	if (FCoreDelegates::GetPakEncryptionKeyDelegate().IsBound())
	{
		FGuid Guid;
		FNamedAESKey& NamedAESKey = KeyChain.GetEncryptionKeys().FindOrAdd(Guid);
		NamedAESKey.Guid = Guid;
		NamedAESKey.Name = TEXT("Embedded");
		FCoreDelegates::GetPakEncryptionKeyDelegate().Execute(NamedAESKey.Key.Key);
		KeyChain.SetPrincipalEncryptionKey(&NamedAESKey);
	}

	return;
}
