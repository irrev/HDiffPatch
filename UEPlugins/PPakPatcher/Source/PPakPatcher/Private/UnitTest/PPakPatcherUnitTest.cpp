#include "UnitTest/PPakPatcherUnitTest.h"
#include "Misc/LazySingleton.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"


#include "Utils/PPakPatcherUtils.h"


FPPakPatcherUnitTest& FPPakPatcherUnitTest::Get()
{
	return TLazySingleton<FPPakPatcherUnitTest>::Get();
}

bool FPPakPatcherUnitTest::SimpleTest()
{
	FString TestSampleDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Plugins/PPakPatcher/Content/TestSample"));
	const FString NewFile = TestSampleDir / TEXT("New/pakchunk2600_0.ucas");
	const FString OldFile = TestSampleDir / TEXT("Old/pakchunk2600_0.ucas");
	bool bSuccess = true;

	bSuccess &= bUseBinaryPatcher ? TestBinaryPatcher(NewFile, OldFile) : true;
	bSuccess &= bUsePakPatcher ? TestPakPatcher(NewFile, OldFile) : true;

	return true;
}

bool FPPakPatcherUnitTest::DirecotryDiffPatchTest(const FString& InNewDir, const FString& InOldDir, const FString& InOutputDir, const FString& InPatchDir, const FString& InNewPatchedDir)
{
	TArray<FPFileCompareInfo> DirCompareInfo = FPPakPatcherUtils::CompareDirectories(InNewDir, InOldDir);
	IPBinPatcher* BinPatcher = IPPakPatcherModule::Get().GetBinPatcher();

	struct FPatchStats
	{
		FString FileName;
		bool bIsEqual = false;
		FString CompressAlgo;
		int64 NewSize;
		int64 OldSize;
		int64 PatchSize;
		int64 PatchFileSize;

		double Duration_CreateDiff = 0;
		double Duration_CheckDiff = 0;
		double Duration_Patch = 0;
		double Duration_MemCompare = 0;
		double Duration_WritePatchFile = 0;
		double Duration_WritePatchedFile = 0;
	};
	TArray<FPatchStats> AllStats;

	if (FPaths::DirectoryExists(InOutputDir))
	{
		IFileManager::Get().DeleteDirectory(*InOutputDir);
		IFileManager::Get().MakeDirectory(*InOutputDir);
	}

	if (FPaths::DirectoryExists(InPatchDir))
	{
		IFileManager::Get().DeleteDirectory(*InPatchDir);
		IFileManager::Get().MakeDirectory(*InPatchDir);
	}

	if (FPaths::DirectoryExists(InNewPatchedDir))
	{
		IFileManager::Get().DeleteDirectory(*InNewPatchedDir);
		IFileManager::Get().MakeDirectory(*InNewPatchedDir);
	}

	for (int32 i = 0; i < DirCompareInfo.Num(); i++)
	{
		FPFileCompareInfo& Info = DirCompareInfo[i];
		const FString NewFile = Info.NewFullPath;
		const FString OldFile = Info.OldFullPath;

		UE_LOG(LogPPakPacher, Display, TEXT("Test BinaryPatch[%d/%d]. NewPakFile:%s, OldPakFile:%s"), i + 1, DirCompareInfo.Num(), *NewFile, *OldFile);

		if (Info.DiffType != EPFileCompareDiffType::Modify)
		{
			UE_LOG(LogPPakPacher, Display, TEXT("Skip patch by not Modify File:%s"), *Info.Filename);
			continue;
		}

		TArray64<uint8> NewData;
		TArray64<uint8> OldData;

		if (!FPPakPatcherUtils::LoadFileToBuffer(NewFile, NewData))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("Error opening new file.%s"), *NewFile);
			continue;
		}

		if (!FPPakPatcherUtils::LoadFileToBuffer(OldFile, OldData))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("Error opening old file.%s"), *OldFile);
			continue;
		}

		if (NewFile.Contains(TEXT("pakchunk1200_0.ucas")))
		{
			int32 aaa = 1;
		}

		auto TestPatch = [&](FString TestName, bool bUseSingleCompressMode, EPakPatchCompressType CompressType) -> bool
			{
				UE_LOG(LogPPakPacher, Display, TEXT("Test BinaryPatch. TestName:%s"), *TestName);
				BinPatcher->bUseSingleCompressMode = bUseSingleCompressMode;

				FPatchStats Stats;
				Stats.FileName = Info.Filename; // Use full relative path
				Stats.CompressAlgo = TestName;
				Stats.NewSize = NewData.Num();
				Stats.OldSize = OldData.Num();

				TArray<uint8> DiffData;
				double StartTime = FPlatformTime::Seconds();
				BinPatcher->CreateDiff(NewData.GetData(), NewData.Num(), OldData.GetData(), OldData.Num(), DiffData, CompressType);
				Stats.Duration_CreateDiff = (FPlatformTime::Seconds() - StartTime);
				UE_LOG(LogPPakPacher, Display, TEXT("  CreateDiff Cost: %.4f s"), Stats.Duration_CreateDiff);

				StartTime = FPlatformTime::Seconds();
				bool bCheckSuccess = BinPatcher->CheckDiff(NewData.GetData(), NewData.Num(), OldData.GetData(), OldData.Num(), DiffData.GetData(), DiffData.Num());
				Stats.Duration_CheckDiff = (FPlatformTime::Seconds() - StartTime);
				UE_LOG(LogPPakPacher, Display, TEXT("  CheckDiff Cost: %.4f s"), Stats.Duration_CheckDiff);

				if (!bCheckSuccess)
				{
					UE_LOG(LogPPakPacher, Error, TEXT("CheckDiff Failed."));
					return false;
				}

				TArray64<uint8> PatchedData;
				PatchedData.SetNumZeroed(NewData.Num());

				StartTime = FPlatformTime::Seconds();
				BinPatcher->Patch(PatchedData.GetData(), PatchedData.Num(), OldData.GetData(), OldData.Num(), DiffData.GetData(), DiffData.Num());
				Stats.Duration_Patch = (FPlatformTime::Seconds() - StartTime);
				UE_LOG(LogPPakPacher, Display, TEXT("  Patch Cost: %.4f s"), Stats.Duration_Patch);

				StartTime = FPlatformTime::Seconds();
				bool bIsSame = FMemory::Memcmp(PatchedData.GetData(), NewData.GetData(), PatchedData.Num()) == 0;
				Stats.Duration_MemCompare = (FPlatformTime::Seconds() - StartTime);
				UE_LOG(LogPPakPacher, Display, TEXT("  MemCompare Cost: %.4f s"), Stats.Duration_MemCompare);

				if (!bIsSame)
				{
					UE_LOG(LogPPakPacher, Error, TEXT("Memory Compare Failed."));
					return false;
				}

				int64 PatchFileSize = 0;
				if (!InPatchDir.IsEmpty())
				{
					// Use Info.Filename to keep directory structure, avoid name conflict.
					FString PatchFile = InPatchDir / TestName / Info.Filename + TEXT(".patch");
					FString PatchFileDir = FPaths::GetPath(PatchFile);
					IFileManager::Get().MakeDirectory(*PatchFileDir, true);

					StartTime = FPlatformTime::Seconds();
					FArchive* Writer = IFileManager::Get().CreateFileWriter(*PatchFile);
					if (Writer)
					{
						Writer->Serialize(DiffData.GetData(), DiffData.Num());
						Writer->Close();

						PatchFileSize = FPPakPatcherUtils::GetFileSize(PatchFile);
					}
					else
					{
						UE_LOG(LogPPakPacher, Error, TEXT("Fail to create patch writer. file: %s"), *PatchFile);
					}
					Stats.Duration_WritePatchFile = (FPlatformTime::Seconds() - StartTime);
					UE_LOG(LogPPakPacher, Display, TEXT("  WritePatchFile Cost: %.4f s"), Stats.Duration_WritePatchFile);
				}

				if (!InNewPatchedDir.IsEmpty())
				{
					FString NewPatchedFile = InNewPatchedDir / TestName / Info.Filename;
					FString NewPatchedFileDir = FPaths::GetPath(NewPatchedFile);
					IFileManager::Get().MakeDirectory(*NewPatchedFileDir, true);

					StartTime = FPlatformTime::Seconds();
					FArchive* Writer = IFileManager::Get().CreateFileWriter(*NewPatchedFile);
					if (Writer)
					{
						Writer->Serialize(PatchedData.GetData(), PatchedData.Num());
						Writer->Close();
					}
					else
					{
						UE_LOG(LogPPakPacher, Error, TEXT("Fail to create new patched file writer. file: %s"), *NewPatchedFile);
					}
					Stats.Duration_WritePatchedFile = (FPlatformTime::Seconds() - StartTime);
					UE_LOG(LogPPakPacher, Display, TEXT("  WritePatchedFile Cost: %.4f s"), Stats.Duration_WritePatchedFile);
				}

				Stats.PatchSize = DiffData.Num();
				Stats.PatchFileSize = PatchFileSize;
				AllStats.Add(Stats);
				return true;
			};


		bool bSuccess = true;
		//bSuccess &= TestPatch(TEXT("Origin"), false, EPakPatchCompressType::None);

		TArray<EPakPatchCompressType> TestCompressTypes = {
		EPakPatchCompressType::ZLIB,
		//EPakPatchCompressType::ZSTD,
		};
		for (EPakPatchCompressType CompressType : TestCompressTypes)
		{
			FString CompressTypeStr = UEnum::GetValueAsString(CompressType);
			int32 ScopeIndex = CompressTypeStr.Find(TEXT("::"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			if (ScopeIndex != INDEX_NONE)
			{
				CompressTypeStr = CompressTypeStr.RightChop(ScopeIndex + 2);
			}
			bSuccess &= TestPatch(CompressTypeStr, true, CompressType);
		}
	}

	const double MB = 1024.0 * 1024.0;

	FString Csv1 = TEXT("FileName,CompressAlgo,NewSize(MB),OldSize(MB),CreateDiff(s),CheckDiff(s),Patch(s),MemCompare(s),WritePatch(s),WritePatched(s),PatchSize(MB),Patch/New(%),PatchFileSize(MB),PatchFileCompression(%),PatchFile/New(%)\n");
	for (const auto& Stat : AllStats)
	{
		double PatchToNew = Stat.NewSize > 0 ? (double)Stat.PatchSize / (double)Stat.NewSize * 100.0 : 0.0;
		double PatchFileComp = Stat.PatchSize > 0 ? (double)Stat.PatchFileSize / (double)Stat.PatchSize * 100.0 : 0.0;
		double PatchFileToNew = Stat.NewSize > 0 ? (double)Stat.PatchFileSize / (double)Stat.NewSize * 100.0 : 0.0;

		Csv1 += FString::Printf(TEXT("%s,%s,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.2f,%.4f,%.2f,%.2f\n"),
			*Stat.FileName, *Stat.CompressAlgo,
			Stat.NewSize / MB, Stat.OldSize / MB,
			Stat.Duration_CreateDiff, Stat.Duration_CheckDiff, Stat.Duration_Patch, Stat.Duration_MemCompare, Stat.Duration_WritePatchFile, Stat.Duration_WritePatchedFile,
			Stat.PatchSize / MB, PatchToNew,
			Stat.PatchFileSize / MB, PatchFileComp, PatchFileToNew);
	}

	UE_LOG(LogPPakPacher, Display, TEXT("Table 1 (Detailed):\\n%s"), *Csv1);
	if (!InOutputDir.IsEmpty())
	{
		FFileHelper::SaveStringToFile(Csv1, *(InOutputDir / TEXT("DetailedStats.csv")));
	}

	struct FAlgoStats
	{
		int64 TotalNewSize = 0;
		int64 TotalOldSize = 0;
		int64 TotalPatchSize = 0;
		int64 TotalPatchFileSize = 0;

		double TotalDuration_CreateDiff = 0;
		double TotalDuration_CheckDiff = 0;
		double TotalDuration_Patch = 0;
		double TotalDuration_MemCompare = 0;
		double TotalDuration_WritePatchFile = 0;
		double TotalDuration_WritePatchedFile = 0;
	};
	TMap<FString, FAlgoStats> AlgoStatsMap;

	for (const auto& Stat : AllStats)
	{
		FAlgoStats& AlgoStat = AlgoStatsMap.FindOrAdd(Stat.CompressAlgo);
		AlgoStat.TotalNewSize += Stat.NewSize;
		AlgoStat.TotalOldSize += Stat.OldSize;
		AlgoStat.TotalPatchSize += Stat.PatchSize;
		AlgoStat.TotalPatchFileSize += Stat.PatchFileSize;

		AlgoStat.TotalDuration_CreateDiff += Stat.Duration_CreateDiff;
		AlgoStat.TotalDuration_CheckDiff += Stat.Duration_CheckDiff;
		AlgoStat.TotalDuration_Patch += Stat.Duration_Patch;
		AlgoStat.TotalDuration_MemCompare += Stat.Duration_MemCompare;
		AlgoStat.TotalDuration_WritePatchFile += Stat.Duration_WritePatchFile;
		AlgoStat.TotalDuration_WritePatchedFile += Stat.Duration_WritePatchedFile;
	}

	FString Csv2 = TEXT("CompressAlgo,TotalNewSize(MB),TotalOldSize(MB),TotalCreateDiff(s),TotalCheckDiff(s),TotalPatch(s),TotalMemCompare(s),TotalWritePatch(s),TotalWritePatched(s),TotalPatchSize(MB),TotalPatch/TotalNew(%),TotalPatchFileSize(MB),TotalPatchFileCompression(%),TotalPatchFile/TotalNew(%)\n");
	for (auto& Elem : AlgoStatsMap)
	{
		FString Algo = Elem.Key;
		FAlgoStats& S = Elem.Value;

		double PatchToNew = S.TotalNewSize > 0 ? (double)S.TotalPatchSize / (double)S.TotalNewSize * 100.0 : 0.0;
		double PatchFileComp = S.TotalPatchSize > 0 ? (double)S.TotalPatchFileSize / (double)S.TotalPatchSize * 100.0 : 0.0;
		double PatchFileToNew = S.TotalNewSize > 0 ? (double)S.TotalPatchFileSize / (double)S.TotalNewSize * 100.0 : 0.0;

		Csv2 += FString::Printf(TEXT("%s,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.2f,%.4f,%.2f,%.2f\n"),
			*Algo, S.TotalNewSize / MB, S.TotalOldSize / MB,
			S.TotalDuration_CreateDiff, S.TotalDuration_CheckDiff, S.TotalDuration_Patch, S.TotalDuration_MemCompare, S.TotalDuration_WritePatchFile, S.TotalDuration_WritePatchedFile,
			S.TotalPatchSize / MB, PatchToNew, S.TotalPatchFileSize / MB, PatchFileComp, PatchFileToNew);
	}

	UE_LOG(LogPPakPacher, Display, TEXT("Table 2 (Summary):\\n%s"), *Csv2);
	if (!InOutputDir.IsEmpty())
	{
		FFileHelper::SaveStringToFile(Csv2, *(InOutputDir / TEXT("SummaryStats.csv")));
	}

	return true;
}



bool FPPakPatcherUnitTest::TestBinaryPatcher(const FString& InNewFile, const FString& InOldFile)
{
	UE_LOG(LogPPakPacher, Display, TEXT("Begin TestBinaryPatch. NewPakFile:%s, OldPakFile:%s"), *InNewFile, *InOldFile);
	const double StartTime = FPlatformTime::Seconds();

	TArray64<uint8> OldData;
	TArray64<uint8> NewData;

	if (!FPPakPatcherUtils::LoadFileToBuffer(InOldFile, OldData))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("Error opening old file.%s"), *InOldFile);
		return false;
	}

	if (!FPPakPatcherUtils::LoadFileToBuffer(InNewFile, NewData))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("Error opening old file.%s"), *InOldFile);
		return false;
	}
	IPBinPatcher* BinPatcher = IPPakPatcherModule::Get().GetBinPatcher();

	TArray<uint8> DiffData;
	BinPatcher->CreateDiff(NewData.GetData(), NewData.Num(), OldData.GetData(), OldData.Num(), DiffData);

	bool bCheckSuccess = BinPatcher->CheckDiff(NewData.GetData(), NewData.Num(), OldData.GetData(), OldData.Num(), DiffData.GetData(), DiffData.Num());
	if (bCheckSuccess)
	{
		UE_LOG(LogPPakPacher, Display, TEXT("CheckDiff Success."));
	}
	else
	{
		UE_LOG(LogPPakPacher, Error, TEXT("CheckDiff Failed."));
		return false;
	}

	TArray64<uint8> PatchedData;
	PatchedData.SetNumZeroed(NewData.Num());
	BinPatcher->Patch(PatchedData.GetData(), PatchedData.Num(), OldData.GetData(), OldData.Num(), DiffData.GetData(), DiffData.Num());

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

bool FPPakPatcherUnitTest::TestPakPatcher(const FString& InNewPakFile, const FString& InOldPakFile)
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

	if (NewPatchData->IsEqual(*PatchData.Get()))
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