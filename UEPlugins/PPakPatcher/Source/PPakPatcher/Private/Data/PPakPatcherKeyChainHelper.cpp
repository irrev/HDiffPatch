#include "Data/PPakPatcherKeyChainHelper.h"
#include "Data/PPakPacherDataType.h"

#include "Misc/LazySingleton.h"
#include "Misc/IEngineCrypto.h"
#include "Misc/KeyChainUtilities.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Base64.h"
#include "RSA.h"

#include "HAL/FileManager.h"

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
	return KeyChain.SigningKey != InvalidRSAKeyHandle && !FParse::Param(FCommandLine::Get(), TEXT("fileopenlog"));
}

bool FPPakPatcherKeyChainHelper::HasEncryptionKey()
{
	return KeyChain.EncryptionKeys.Num() > 0;
}

void FPPakPatcherKeyChainHelper::LoadKeyChain(bool bForceReload)
{
	if (bLoadOnce || bForceReload)
	{
		bLoadOnce = false;

		KeyChain.SigningKey = InvalidRSAKeyHandle;
		KeyChain.EncryptionKeys.Empty();

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
		KeyChain.MasterEncryptionKey = KeyChain.EncryptionKeys.Find(EncryptionKeyOverrideGuid);
	}
}

bool FPPakPatcherKeyChainHelper::LoadKeyChainFromFile()
{
	// 编辑器模式下，可以通过Crypto.json来获取。构建后会在: Saved/Cooked/[Platform]/[Game]/Metadata/Crypto.json

	// First, try and parse the keys from a supplied crypto key cache file
	const TCHAR* CmdLine = FCommandLine::Get();
	FString CryptoKeysCacheFilename;
	FParse::Value(CmdLine, TEXT("cryptokeys="), CryptoKeysCacheFilename);
	if (CryptoKeysCacheFilename.IsEmpty())
	{
		return false;
	}

	if (IFileManager::Get().FileExists(*CryptoKeysCacheFilename))
	{
		UE_LOG(LogPPakPacher, Display, TEXT("Parsing crypto keys from a crypto key cache file"));
		KeyChainUtilities::LoadKeyChainFromFile(CryptoKeysCacheFilename, KeyChain);
		return true;
	}
	return true;
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

				KeyChain.SigningKey = FRSA::CreateKey(PublicExp, PrivateExp, Modulus);

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
					KeyChain.EncryptionKeys.Add(NewKey.Guid, NewKey);
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
					KeyChain.EncryptionKeys.Add(NewKey.Guid, NewKey);
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
			return false;
		}

		if (EncryptionKeyString.Len() > RequiredKeyLength)
		{
			UE_LOG(LogPPakPacher, Warning, TEXT("AES encryption key is more than %d characters long, so will be truncated!"), RequiredKeyLength);
			EncryptionKeyString.LeftInline(RequiredKeyLength);
		}

		if (!FCString::IsPureAnsi(*EncryptionKeyString))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("AES encryption key must be a pure ANSI string!"));
			return false;
		}

		ANSICHAR* AsAnsi = TCHAR_TO_ANSI(*EncryptionKeyString);
		check(TCString<ANSICHAR>::Strlen(AsAnsi) == RequiredKeyLength);
		FMemory::Memcpy(NewKey.Key.Key, AsAnsi, RequiredKeyLength);
		KeyChain.EncryptionKeys.Add(NewKey.Guid, NewKey);
		UE_LOG(LogPPakPacher, Display, TEXT("Parsed AES encryption key from command line."));
		return true;
	}
	return false;
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
		KeyChain.SigningKey = FRSA::CreateKey(Exponent, TArray<uint8>(), Modulus);
	}

	// encrypt key
	if (FCoreDelegates::GetPakSigningKeysDelegate().IsBound())
	{
		FGuid Guid;
		FNamedAESKey& NamedAESKey = KeyChain.EncryptionKeys.FindOrAdd(Guid);
		NamedAESKey.Guid = Guid;
		NamedAESKey.Name = TEXT("Embedded");
		FCoreDelegates::GetPakEncryptionKeyDelegate().Execute(NamedAESKey.Key.Key);
		KeyChain.MasterEncryptionKey = &NamedAESKey;
	}

	return;
}
