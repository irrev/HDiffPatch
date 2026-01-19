#pragma once

#include "CoreMinimal.h"
#include "IPlatformFilePak.h"
#include "Templates/RefCounting.h"
#include "KeyChainUtilities.h"


class PPAKPATCHER_API FPPakFileData
{
public:
	FPPakFileData();
	~FPPakFileData();
	
	bool LoadFromFile(const FString& InPakFilename);

	FArchive* GetSharedReader(IPlatformFile* LowerLevel = nullptr);
	bool Compare(FPPakFileData& Other);
	const FString& GetPakFileMD5();

	FString PakFilename;
	bool bIsSigned = false;
	FPakInfo PakInfo;
	TRefCountPtr<class FPakFile> PakFilePtr;
	TArray<FPakEntryPair> Index;
private:
	bool PreCheckDecript();
	bool ValidateEncryptionKey(TArray<uint8>& IndexData, const FSHAHash& InExpectedHash, const FAES::FAESKey& InAESKey);
	bool TryDecryptPak(FArchive* InReader, const FPakInfo& InPakInfo, const FNamedAESKey& InKey);
	FString FileMD5;
	const FNamedAESKey* NamedAESKey = nullptr;
};



typedef TSharedPtr<FPPakFileData, ESPMode::ThreadSafe> FPPakFileDataPtr;