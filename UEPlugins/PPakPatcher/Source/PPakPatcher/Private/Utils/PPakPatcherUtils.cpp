#include "Utils/PPakPatcherUtils.h"
#include "Data/PPakPatcherDataType.h"

#include "PPakPatcherModule.h"

#include "HAL/FileManager.h"

bool FPPakPatcherUtils::LoadFileToBuffer(const FString& InFileName, TArray<uint8>& OutBuffer)
{
	FArchive* Reader = IFileManager::Get().CreateFileReader(*InFileName, EFileRead::FILEREAD_None);
	if (!Reader)
	{
		return false;
	}

	if (Reader->Tell() != 0)
	{
		UE_LOG(LogPPakPacher, Warning, TEXT("Archive '%s' has already been read from."), *InFileName);
		return false;
	}

	OutBuffer.Empty();
	int64 Size = Reader->TotalSize();
	if (Size == 0)
	{
		return true;
	}

	if (Reader->Tell() != 0)
	{
		UE_LOG(LogPPakPacher, Warning, TEXT("Archive '%s' has already been read from."), *InFileName);
		return false;
	}
	OutBuffer.SetNumZeroed(Size);
	Reader->Serialize(OutBuffer.GetData(), Size);
	bool Success = Reader->Close();
	return Success;
}

bool FPPakPatcherUtils::TestBinaryPatch(const FString& InNewFile, const FString& InOldFile)
{
	UE_LOG(LogPPakPacher, Display, TEXT("Begin TestBinaryPatch. NewPakFile:%s, OldPakFile:%s"), *InNewFile, *InOldFile);
	const double StartTime = FPlatformTime::Seconds();

	TArray<uint8> OldData;
	TArray<uint8> NewData;

	if (!LoadFileToBuffer(InOldFile, OldData))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("Error opening old file.%s"), *InOldFile);
		return false;
	}

	if (!LoadFileToBuffer(InNewFile, NewData))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("Error opening old file.%s"), *InOldFile);
		return false;
	}
	IPBinPatcher* BinPatcher = IPPakPatcherModule::Get().GetBinPatcher();

	TArray<uint8> DiffData;
	BinPatcher->CreateDiff(NewData, OldData, DiffData);

	bool bCheckSuccess = BinPatcher->CheckDiff(NewData, OldData, DiffData);
	if (bCheckSuccess)
	{
		UE_LOG(LogPPakPacher, Display, TEXT("CheckDiff Success."));
	}
	else
	{
		UE_LOG(LogPPakPacher, Error, TEXT("CheckDiff Failed."));
		return false;
	}

	TArray<uint8> PatchedData;
	PatchedData.SetNumZeroed(NewData.Num());
	BinPatcher->Patch(PatchedData, OldData, DiffData);

	bool bIsSame = FMemory::Memcmp(PatchedData.GetData(), NewData.GetData(), PatchedData.Num()) == 0;
	if (bIsSame)
	{
		UE_LOG(LogPPakPacher, Display, TEXT("Memory Compare Success."));
	}
	else
	{
		UE_LOG(LogPPakPacher, Error, TEXT("Memory Compare Failed."));
		return false;
	}

	UE_LOG(LogPPakPacher, Display, TEXT("TestBinaryPatch successed. Cost time %.2lfs."), FPlatformTime::Seconds() - StartTime);

	return true;
}

bool FPPakPatcherUtils::TestPakPatch(const FString& InNewPakFile, const FString& InOldPakFile)
{
	//ExecuteUnrealPak(*FString::Printf(TEXT("-Diff %s %s"), *InNewPakFile, *InOldPakFile));
	UE_LOG(LogPPakPacher, Display, TEXT("Begin TestPakPatch. NewPakFile:%s, OldPakFile:%s"), *InNewPakFile, *InOldPakFile);
	const double StartTime = FPlatformTime::Seconds();

	IPPakPatcher* PakPatcher = IPPakPatcherModule::Get().GetPakPatcher();

	FPPakFileDataPtr OldPakFile = MakeShareable(new FPPakFileData());
	FPPakFileDataPtr NewPakFile = MakeShareable(new FPPakFileData());

	if (!OldPakFile->LoadFromFile(InOldPakFile))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("Error load old pak.%s"), *InOldPakFile);
		return false;
	}

	if (!NewPakFile->LoadFromFile(InNewPakFile))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("Error load new pak.%s"), *InNewPakFile);
		return false;
	}
	FString PathPart, FilenamePart, ExtPart;
	FPaths::Split(InOldPakFile, PathPart, FilenamePart, ExtPart);
	FString PatchFileName = FPaths::Combine(PathPart, FilenamePart) + TEXT(".patch");

	// step 1: generate patch data.
	FPPakPatchDataPtr PatchData;
	if (PakPatcher->CreatePakDiff(PatchFileName, NewPakFile, OldPakFile, PatchData))
	{
		UE_LOG(LogPPakPacher, Display, TEXT("CreatePakDiff Success."));
	}
	else
	{
		UE_LOG(LogPPakPacher, Error, TEXT("CreatePakDiff Failed."));
		return false;
	}

	// step 2: save patch data to file.
	if (PatchData->IsUsePrecache())
	{
		if (PatchData->SaveToFile(PatchFileName))
		{
			UE_LOG(LogPPakPacher, Display, TEXT("SaveToFile Success."));
		}
		else
		{
			UE_LOG(LogPPakPacher, Error, TEXT("SaveToFile Failed."));
			return false;
		}
	}
	

	// step 3: load patch data.
	FPPakPatchDataPtr NewPatchData = MakeShareable(new FPPakPatchData());
	if (NewPatchData->LoadFromFile(PatchFileName))
	{
		UE_LOG(LogPPakPacher, Display, TEXT("LoadFromFile Success."));
	}
	else
	{
		UE_LOG(LogPPakPacher, Error, TEXT("LoadFromFile Failed."));
		return false;
	}

	if(NewPatchData->IsEqual(*PatchData.Get()))
	{
		UE_LOG(LogPPakPacher, Display, TEXT("Check Save & Load Patch Data Success."));
	}
	else
	{
		UE_LOG(LogPPakPacher, Error, TEXT("The loaded data is inconsistent with the saved data."));
		return false;
	}

	// write to tmp file for binary file diff. 
	//NewPatchData->SaveToFile(FPaths::Combine(PathPart, TEXT("tmp")) + TEXT(".patch"));

	// step 4: check patch.
	if (PakPatcher->CheckPakDiff(NewPakFile, OldPakFile, NewPatchData))
	{
		UE_LOG(LogPPakPacher, Display, TEXT("CheckDiff Pak Success."));
	}
	else
	{
		UE_LOG(LogPPakPacher, Error, TEXT("CheckDiff Pak Failed."));
		return false;
	}

	// step 5: patch old pak file to new.
	FString PatchedPakFilename = FPaths::ChangeExtension(InNewPakFile, TEXT("newpak"));
	if (PakPatcher->PatchPak(PatchedPakFilename, OldPakFile, NewPatchData))
	{
		UE_LOG(LogPPakPacher, Display, TEXT("Patch Pak Success."));
	}
	else
	{
		UE_LOG(LogPPakPacher, Error, TEXT("Patch Pak Failed."));
		return false;
	}

	// step 6: compare new pak file and patched pak files.
	FPPakFileDataPtr PatchedPakFile = MakeShareable(new FPPakFileData());
	PatchedPakFile->LoadFromFile(PatchedPakFilename);
	bool bIsEqual = false;

	FString NewMD5 = NewPakFile->GetPakFileMD5();
	FString PatchedMD5 = PatchedPakFile->GetPakFileMD5();
	if (NewMD5.IsEmpty())
	{
		FMD5Hash Hash = FMD5Hash::HashFile(*NewPakFile->PakFilename);
		NewMD5 = LexToString(Hash);
	}
	if (PatchedMD5.IsEmpty())
	{
		FMD5Hash Hash = FMD5Hash::HashFile(*PatchedPakFile->PakFilename);
		PatchedMD5 = LexToString(Hash);
	}
	if (NewMD5 == PatchedMD5)
	{
		UE_LOG(LogPPakPacher, Display, TEXT("Compare Patched Pak File with new Success."));
	}
	else
	{
		UE_LOG(LogPPakPacher, Error, TEXT("Compare Patched Pak File with new Failed."));
		return false;
	}

	UE_LOG(LogPPakPacher, Display, TEXT("TestPakPatch successed. Cost time %.2lfs."), FPlatformTime::Seconds() - StartTime);
	
	return true;
}

bool FPPakPatcherUtils::DumpMemoryToFile(const FString& InFilename, uint8* InData, int64 InSize)
{
	FArchive* Writer = IFileManager::Get().CreateFileWriter(*InFilename);
	if (Writer == nullptr)
	{
		UE_LOG(LogPPakPacher, Error, TEXT("Fail to create pak patch writer. file: %s"), *InFilename);
		return false;
	}

	Writer->Serialize(InData, InSize);

	Writer->Close();
	delete Writer;

	return true;
}
