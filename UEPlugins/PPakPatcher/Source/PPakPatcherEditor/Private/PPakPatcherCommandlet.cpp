
#include "PPakPatcherCommandlet.h"
#include "Engine.h"
#include "PPakPatcherModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogPakPatcherCommandlet, Display, All);


int32 UPPakPatcherCommandlet::Main(const FString& Params)
{
	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("UPPakPatcherCommandlet params: %s"), *Params);

	int32 ErrorCode = 0;
	if (FParse::Param(*Params, TEXT("CreatePakPatch")))
	{
		ErrorCode = CreatePakPatch(Params);
	}
	else if (FParse::Param(*Params, TEXT("CheckPakPatch")))
	{
		ErrorCode = CreatePakPatch(Params);
	}
	else if (FParse::Param(*Params, TEXT("PatchPak")))
	{
		ErrorCode = CreatePakPatch(Params);
	}
	else if (FParse::Param(*Params, TEXT("CreatePakPatchWithDir")))
	{
		ErrorCode = CreatePakPatchWithDir(Params);
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
	if (!CheckFileParams(*Params, TEXT("Patch="), PatchFileName, false))
	{
		return -1;
	}
	if (!CreatePakPatch_Internal(NewPakFilename, OldPakFilename, PatchFileName))
	{
		return -1;
	}
	return 0;
}

bool UPPakPatcherCommandlet::CreatePakPatch_Internal(const FString& InNewPakFilename, const FString& InOldPakFilename, const FString& InPatchFilename)
{
	FPPakFileDataPtr NewPakFile = MakeShareable(new FPPakFileData());
	if (!NewPakFile->LoadFromFile(InNewPakFilename))
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("Load new pak failed.%s"), *InNewPakFilename);
		return false;
	}

	FPPakFileDataPtr OldPakFile = MakeShareable(new FPPakFileData());
	if (!OldPakFile->LoadFromFile(InOldPakFilename))
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("Load old pak failed.%s"), *InOldPakFilename);
		return false;
	}

	IPPakPatcher* PakPatcher = IPPakPatcherModule::Get().GetPakPatcher();
	FPPakPatchDataPtr PatchData;
	if (PakPatcher->CreatePakDiff(InPatchFilename, NewPakFile, OldPakFile, PatchData))
	{
		UE_LOG(LogPakPatcherCommandlet, Display, TEXT("CreatePakDiff Successed. %s"), *InPatchFilename);
	}
	else
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("CreatePakDiff Failed. %s"), *InPatchFilename);
		return false;
	}

	// save patch data to file.
	if (PatchData->IsUsePrecache())
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

	FPPakFileDataPtr NewPakFile = MakeShareable(new FPPakFileData());
	if (!NewPakFile->LoadFromFile(NewPakFilename))
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("Load new pak failed.%s"), *NewPakFilename);
		return -1;
	}

	FPPakFileDataPtr OldPakFile = MakeShareable(new FPPakFileData());
	if (!OldPakFile->LoadFromFile(OldPakFilename))
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("Load old pak failed.%s"), *OldPakFilename);
		return -1;
	}

	// load patch data.
	FPPakPatchDataPtr PatchData = MakeShareable(new FPPakPatchData());
	if (PatchData->LoadFromFile(PatchFileName))
	{
		UE_LOG(LogPakPatcherCommandlet, Display, TEXT("LoadFromFile Success."));
	}
	else
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("LoadFromFile Failed."));
		return -1;
	}

	IPPakPatcher* PakPatcher = IPPakPatcherModule::Get().GetPakPatcher();

	if (PakPatcher->CheckPakDiff(NewPakFile, OldPakFile, PatchData))
	{
		UE_LOG(LogPakPatcherCommandlet, Display, TEXT("CheckPakDiff Successed. %s"), *PatchFileName);
	}
	else
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("CheckPakDiff Failed. %s"), *PatchFileName);
		return -1;
	}
	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Finish CheckPakPatch process. %s"), *PatchFileName);

	return 0;
}

int32 UPPakPatcherCommandlet::PatchPak(const FString& Params)
{
	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Begin Patch Pak."));

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

	FPPakFileDataPtr OldPakFile = MakeShareable(new FPPakFileData());
	if (!OldPakFile->LoadFromFile(OldPakFilename))
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("Load old pak failed.%s"), *OldPakFilename);
		return -1;
	}

	// load patch data.
	FPPakPatchDataPtr PatchData = MakeShareable(new FPPakPatchData());
	if (PatchData->LoadFromFile(PatchFileName))
	{
		UE_LOG(LogPakPatcherCommandlet, Display, TEXT("LoadFromFile Success."));
	}
	else
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("LoadFromFile Failed."));
		return -1;
	}

	IPPakPatcher* PakPatcher = IPPakPatcherModule::Get().GetPakPatcher();

	if (PakPatcher->PatchPak(NewPakFilename, OldPakFile, PatchData))
	{
		UE_LOG(LogPakPatcherCommandlet, Display, TEXT("PatchPak Successed. %s"), *PatchFileName);
	}
	else
	{
		UE_LOG(LogPakPatcherCommandlet, Error, TEXT("PatchPak Failed. %s"), *PatchFileName);
		return -1;
	}
	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Finish PatchPak process. %s"), *PatchFileName);

	return 0;
}

TArray<FString> UPPakPatcherCommandlet::GatherPaksInDirectory(const FString InDir)
{
	TArray<FString> Result;
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *InDir, TEXT("*.*"), true, false);

	for (const FString& File : Files)
	{
		if (File.EndsWith(TEXT(".pak")))
		{
			Result.Add(InDir / File);
		}
	}
	return MoveTemp(Result);
}

TMap<FString, FString> UPPakPatcherCommandlet::MakeNewOldMatchMap(const FString& InNewDir, const FString& InOldDir)
{
	TMap<FString, FString> Result;
	auto GetChunkName = [](const FString& InFilename) -> FString
		{
			FString Cleanname = FPaths::GetCleanFilename(InFilename);
			FString Chunkname;
			if (Cleanname.StartsWith(TEXT("chunk")) && Cleanname.EndsWith(TEXT(".pak")))
			{
				Cleanname.Split(TEXT("-"), &Chunkname, nullptr);
			}
			return Chunkname;
		};

	TArray<FString> NewPaks = GatherPaksInDirectory(InNewDir);
	TArray<FString> OldPaks = GatherPaksInDirectory(InOldDir);

	for (const FString& NewPak : NewPaks)
	{
		FString NewChunkname = GetChunkName(NewPak);
		if (!NewChunkname.IsEmpty())
		{
			for (int32 i=0; i< OldPaks.Num(); ++i)
			{
				FString OldChunkname = GetChunkName(OldPaks[i]);
				if (!OldChunkname.IsEmpty())
				{
					if (NewChunkname == OldChunkname)
					{
						Result.Add(NewPak, OldPaks[i]);
						OldPaks.RemoveAt(i);
						i--;
						break;
					}
				}
			}
			Result.Add(NewPak, TEXT(""));
		}
	}
	return MoveTemp(Result);
}

int32 UPPakPatcherCommandlet::CreatePakPatchWithDir(const FString& Params)
{
	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Begin Patch Pak."));

	FString NewDir;
	FString OldDir;
	FString PatchDir;
	bool bCopyNewIfNoOld = Params.Contains(TEXT("-CopyNewIfNoOld"));

	if (!CheckDirParams(*Params, TEXT("NewDir="), NewDir), true)
	{
		return -1;
	}
	if (!CheckDirParams(*Params, TEXT("OldDir="), OldDir), true)
	{
		return -1;
	}
	if (!CheckDirParams(*Params, TEXT("PatchDir="), PatchDir, false, true))
	{
		return -1;
	}

	TMap<FString, FString> NewOldMap = MakeNewOldMatchMap(NewDir, OldDir);
	for (auto It = NewOldMap.CreateConstIterator(); It; ++It)
	{
		const FString& NewPak = It.Key();
		const FString& OldPak = It.Value();
		UE_LOG(LogPakPatcherCommandlet, Display, TEXT("New Pak: %s"), *NewPak);
		UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Old Pak: %s"), *OldPak);

		if (!OldPak.IsEmpty())
		{
			FString OldBasename = FPaths::GetBaseFilename(OldPak);
			FString PatchFilename = PatchDir / OldBasename + TEXT(".patch");

			if (!CreatePakPatch_Internal(NewPak, OldPak, PatchFilename))
			{
				UE_LOG(LogPakPatcherCommandlet, Error, TEXT("CopyFromContent: Failed to genrate patch from new [%s] and old [%s] !"), *NewPak, *OldPak);
				return -1;
			}
		}
		else if (bCopyNewIfNoOld)
		{
			FString CopyFrom = NewPak;
			FString CopyTo = PatchDir / FPaths::GetCleanFilename(NewPak);
			if (COPY_OK != IFileManager::Get().Copy(*CopyTo, *CopyFrom))
			{
				UE_LOG(LogPakPatcherCommandlet, Error, TEXT("CopyFromContent: Failed copy from [%s] to [%s] !"), *CopyFrom, *CopyTo);
				return -1;
			}
		}
		
	}

	UE_LOG(LogPakPatcherCommandlet, Display, TEXT("Finish generate Patch files. output dir:%s"), *PatchDir);
	return 0;
}
