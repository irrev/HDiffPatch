#include "Data/PPakFileData.h"
#include "Data/PPakPacherDataType.h"
#include "Data/PPakPatcherKeyChainHelper.h"
#include "PPakPatcherSettings.h"

#include "IPlatformFilePak.h"
#include "HAL/PlatformFilemanager.h"

FPPakFileData::FPPakFileData()
{

}

FPPakFileData::~FPPakFileData()
{

}

bool FPPakFileData::LoadFromFile(const FString& InPakFilename)
{
	PakFilename = InPakFilename;
	FPaths::MakeStandardFilename(PakFilename);


	if (!IFileManager::Get().FileExists(*PakFilename))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("Can't find pak file of given PakFilename. %s"), *PakFilename);
		return false;
	}

	if (!PreCheckDecript())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("Pak file load failed by decript failed. %s"), *PakFilename);
		return false;
	}

	bIsSigned = FPPakPatcherKeyChainHelper::Get().Signed();
	if (bIsSigned)
	{
		FString PakSignaturesFilename = FPaths::ChangeExtension(PakFilename, TEXT("sig"));
		if (!IFileManager::Get().FileExists(*PakSignaturesFilename))
		{
			bIsSigned = false;
		}
	}
	PakFilePtr = new FPakFile(&FPlatformFileManager::Get().GetPlatformFile(), *PakFilename, bIsSigned);
	FPakFile& PakFile = *PakFilePtr;
	if (!PakFilePtr->IsValid())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("PakFile invalid. %s"), *PakFilename);
		return false;
	}

	if (!PakFilePtr->HasFilenames())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("Pakfiles were loaded without Filenames."));
		return false;
	}

	PakInfo = PakFile.GetInfo();

	return true;
}


FArchive* FPPakFileData::GetSharedReader(IPlatformFile* LowerLevel)
{
	return PakFilePtr->GetSharedReader(LowerLevel);
}

bool FPPakFileData::Compare(FPPakFileData& Other)
{
	return this->GetPakFileMD5() == Other.GetPakFileMD5();
}

const FString& FPPakFileData::GetPakFileMD5()
{
	if (FileMD5.IsEmpty() && FPPakPatcherSettings::Get().bGenPakFileMD5)
	{
		const double StartTime = FPlatformTime::Seconds();
		FMD5Hash Hash = FMD5Hash::HashFile(*PakFilename);
		FileMD5 = LexToString(Hash);
		UE_LOG(LogPPakPacher, Display, TEXT("PakFile Genarete MD5 cost time %.2lfs. Filename:%s"), FPlatformTime::Seconds() - StartTime, *PakFilename);
	}
	return FileMD5;
}

bool FPPakFileData::PreCheckDecript()
{
	UE_LOG(LogPPakPacher, Log, TEXT("FPPakFileData::PreCheckDecript - Pre-check decrpit pak file: %s and check file hash."), *PakFilename);

	FArchive* Reader = IFileManager::Get().CreateFileReader(*PakFilename);
	if (!Reader)
	{
		UE_LOG(LogPPakPacher, Log, TEXT("FPPakFileData::PreCheckDecript - Failed by invalid reader. %s"), *PakFilename);
		return false;
	}

	FPakInfo Info;
	const int64 CachedTotalSize = Reader->TotalSize();
	bool bShouldLoad = false;
	int32 CompatibleVersion = FPakInfo::PakFile_Version_Latest;

	// Serialize trailer and check if everything is as expected.
	// start up one to offset the -- below
	CompatibleVersion++;
	int64 FileInfoPos = -1;
	do
	{
		// try the next version down
		CompatibleVersion--;

		FileInfoPos = CachedTotalSize - Info.GetSerializedSize(CompatibleVersion);
		if (FileInfoPos >= 0)
		{
			Reader->Seek(FileInfoPos);

			// Serialize trailer and check if everything is as expected.
			Info.Serialize(*Reader, CompatibleVersion);
			if (Info.Magic == FPakInfo::PakFile_Magic)
			{
				bShouldLoad = true;
			}
		}
	} while (!bShouldLoad && CompatibleVersion >= FPakInfo::PakFile_Version_Initial);

	if (!bShouldLoad)
	{
		UE_LOG(LogPPakPacher, Error, TEXT("%s is not a valid pak file!"), *PakFilename);
	}

	if(bShouldLoad)
	{
		if (Info.EncryptionKeyGuid.IsValid() || Info.bEncryptedIndex)
		{
			const FNamedAESKey* FoundKey = nullptr;
			FKeyChain& KeyChain = FPPakPatcherKeyChainHelper::Get().GetKeyChain();
			if (KeyChain.MasterEncryptionKey)
			{
				if (TryDecryptPak(Reader, Info, *KeyChain.MasterEncryptionKey))
				{
					FoundKey = KeyChain.MasterEncryptionKey;
				}
			}
			if (FoundKey == nullptr && KeyChain.EncryptionKeys.Num())
			{
				// try other keys.
				for (auto It = KeyChain.EncryptionKeys.CreateConstIterator(); It; ++It)
				{
					const FNamedAESKey& Key = It.Value();
					if (TryDecryptPak(Reader, Info, Key))
					{
						FoundKey = &Key;
						break;
					}
				}
			}
			if (FoundKey != nullptr)
			{
				NamedAESKey = FoundKey;
				UE_LOG(LogPPakPacher, Log, TEXT("Found valid decript key [%s]!"), *NamedAESKey->Name);
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION >= 26
				FCoreDelegates::GetRegisterEncryptionKeyMulticastDelegate().Broadcast(NamedAESKey->Guid, NamedAESKey->Key);
#else
				FCoreDelegates::GetRegisterEncryptionKeyDelegate().ExecuteIfBound(NamedAESKey->Guid, NamedAESKey->Key);
#endif
			}
			else
			{
				UE_LOG(LogPPakPacher, Error, TEXT("Can't open encrypt pak without OnGetAESKey bound!"));
				bShouldLoad = false;
			}
		}
	}
	Reader->Close();
	delete Reader;
	return bShouldLoad;
}


bool FPPakFileData::ValidateEncryptionKey(TArray<uint8>& IndexData, const FSHAHash& InExpectedHash, const FAES::FAESKey& InAESKey)
{
	FAES::DecryptData(IndexData.GetData(), IndexData.Num(), InAESKey);

	// Check SHA1 value.
	FSHAHash ActualHash;
	FSHA1::HashBuffer(IndexData.GetData(), IndexData.Num(), ActualHash.Hash);
	return InExpectedHash == ActualHash;
}

bool FPPakFileData::TryDecryptPak(FArchive* InReader, const FPakInfo& InPakInfo, const FNamedAESKey& InKey)
{
	UE_LOG(LogPPakPacher, Log, TEXT("Try decript pak with key [%s]!"), *InKey.Name);
	bool bShouldLoad = true;

	TArray<uint8> PrimaryIndexData;
	InReader->Seek(InPakInfo.IndexOffset);
	PrimaryIndexData.SetNum(InPakInfo.IndexSize);
	InReader->Serialize(PrimaryIndexData.GetData(), InPakInfo.IndexSize);

	if (!ValidateEncryptionKey(PrimaryIndexData, InPakInfo.IndexHash, InKey.Key))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("AES encryption key [%s] is not correct!"), *InKey.Name);
		bShouldLoad = false;
	}

	return bShouldLoad;
}
