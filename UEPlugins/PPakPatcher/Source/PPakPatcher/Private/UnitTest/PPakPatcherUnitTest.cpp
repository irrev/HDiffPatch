#include "UnitTest/PPakPatcherUnitTest.h"
#include "Misc/LazySingleton.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"


#include "Patcher/FPResPatcher.h"
#include "Utils/PPakPatcherUtils.h"


FPPakPatcherUnitTest& FPPakPatcherUnitTest::Get()
{
	return TLazySingleton<FPPakPatcherUnitTest>::Get();
}

bool FPPakPatcherUnitTest::SimpleTest()
{
	FString TestSampleDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Plugins/PPakPatcher/Content/TestSample"));
	// 用 .pak 作为测试入口；FPResPatcher 会自动联动同伴 .utoc/.ucas 处理 IoStore。
	// 直接传 .ucas/.utoc 现在会被 FPResPatcher 拒绝（必须传 .pak）。
	const FString NewFile = TestSampleDir / TEXT("New/pakchunk2600_0.pak");
	const FString OldFile = TestSampleDir / TEXT("Old/pakchunk2600_0.pak");
	bool bSuccess = true;

	bSuccess &= bUseBinaryPatcher ? TestBinaryPatcher(NewFile, OldFile) : true;
	bSuccess &= bUsePakPatcher    ? TestPakPatcher(NewFile, OldFile)    : true;

	UE_LOG(LogPPakPacher, Display, TEXT("FPPakPatcherUnitTest::SimpleTest - Done. bSuccess=%s"),
		bSuccess ? TEXT("true") : TEXT("false"));
	return bSuccess;
}

bool FPPakPatcherUnitTest::DirecotryDiffPatchTest(const FString& InNewDir, const FString& InOldDir, const FString& InOutputDir, const FString& InPatchDir, const FString& InNewPatchedDir)
{
	TArray<FPFileCompareInfo> DirCompareInfo = FPPakPatcherUtils::CompareDirectories(InNewDir, InOldDir);
	FPResPatcher ResPatcher;

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

		auto TestPatch = [&](FString TestName, EPakPatchCompressType CompressType) -> bool
			{
				UE_LOG(LogPPakPacher, Display, TEXT("Test BinaryPatch. TestName:%s"), *TestName);

				FPatchStats Stats;
				Stats.FileName = Info.Filename; // Use full relative path
				Stats.CompressAlgo = TestName;
				Stats.NewSize = NewData.Num();
				Stats.OldSize = OldData.Num();

				// Patch 输出路径（用于 CreateDiff 直接落盘）
				FString PatchFile;
				if (!InPatchDir.IsEmpty())
				{
					PatchFile = InPatchDir / TestName / Info.Filename + TEXT(".patch");
					FString PatchFileDir = FPaths::GetPath(PatchFile);
					IFileManager::Get().MakeDirectory(*PatchFileDir, true);
				}

				// CreateDiff : 内部已负责写补丁文件
				FPResPatchDataPtr PatchData;
				double StartTime = FPlatformTime::Seconds();
				const bool bCreateOk = ResPatcher.CreateDiff(PatchFile, NewFile, OldFile, PatchData,
					EPPakPatchMode::PakAware, CompressType);
				Stats.Duration_CreateDiff = (FPlatformTime::Seconds() - StartTime);
				UE_LOG(LogPPakPacher, Display, TEXT("  CreateDiff Cost: %.4f s"), Stats.Duration_CreateDiff);
				if (!bCreateOk)
				{
					UE_LOG(LogPPakPacher, Error, TEXT("CreateDiff Failed."));
					return false;
				}

				// CheckDiff
				StartTime = FPlatformTime::Seconds();
				const bool bCheckSuccess = ResPatcher.CheckDiff(NewFile, OldFile, PatchData);
				Stats.Duration_CheckDiff = (FPlatformTime::Seconds() - StartTime);
				UE_LOG(LogPPakPacher, Display, TEXT("  CheckDiff Cost: %.4f s"), Stats.Duration_CheckDiff);
				if (!bCheckSuccess)
				{
					UE_LOG(LogPPakPacher, Error, TEXT("CheckDiff Failed."));
					return false;
				}

				// PatchDiff : 产出 PatchedFile（直接落盘）
				FString NewPatchedFile;
				if (!InNewPatchedDir.IsEmpty())
				{
					NewPatchedFile = InNewPatchedDir / TestName / Info.Filename;
					FString NewPatchedFileDir = FPaths::GetPath(NewPatchedFile);
					IFileManager::Get().MakeDirectory(*NewPatchedFileDir, true);
				}
				else
				{
					NewPatchedFile = FPaths::ChangeExtension(NewFile, TEXT("patched"));
				}

				StartTime = FPlatformTime::Seconds();
				const bool bPatchOk = ResPatcher.PatchDiff(NewPatchedFile, OldFile, PatchData);
				Stats.Duration_Patch = (FPlatformTime::Seconds() - StartTime);
				UE_LOG(LogPPakPacher, Display, TEXT("  Patch Cost: %.4f s"), Stats.Duration_Patch);
				if (!bPatchOk)
				{
					UE_LOG(LogPPakPacher, Error, TEXT("Patch Failed."));
					return false;
				}

				// MemCompare : 用 MD5 文件比较代替内存 memcmp
				StartTime = FPlatformTime::Seconds();
				const FMD5Hash NewHash = FMD5Hash::HashFile(*NewFile);
				const FMD5Hash PatchedHash = FMD5Hash::HashFile(*NewPatchedFile);
				const bool bIsSame = LexToString(NewHash) == LexToString(PatchedHash);
				Stats.Duration_MemCompare = (FPlatformTime::Seconds() - StartTime);
				UE_LOG(LogPPakPacher, Display, TEXT("  MemCompare Cost: %.4f s"), Stats.Duration_MemCompare);
				if (!bIsSame)
				{
					UE_LOG(LogPPakPacher, Error, TEXT("Memory Compare Failed."));
					return false;
				}

				// CreateDiff / PatchDiff 已经负责落盘，额外的写补丁/写 patched 阶段耗时合并为 0。
				Stats.Duration_WritePatchFile = 0;
				Stats.Duration_WritePatchedFile = 0;

				const int64 PatchFileSize = PatchFile.IsEmpty() ? 0 : FPPakPatcherUtils::GetFileSize(PatchFile);
				Stats.PatchSize = PatchFileSize; // 走 FPResPatcher 后不再单独持有内存 diff 字节，以补丁文件大小近似 PatchSize
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
			bSuccess &= TestPatch(CompressTypeStr, CompressType);
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

	FPResPatcher ResPatcher;

	const FString PatchFile = FPaths::ChangeExtension(InOldFile, TEXT("patch"));
	const FString PatchedFile = FPaths::ChangeExtension(InNewFile, TEXT("patched"));

	// CreateDiff
	FPResPatchDataPtr PatchData;
	if (!ResPatcher.CreateDiff(PatchFile, InNewFile, InOldFile, PatchData))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("CreateDiff Failed."));
		return false;
	}

	// CheckDiff
	if (!ResPatcher.CheckDiff(InNewFile, InOldFile, PatchData))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("CheckDiff Failed."));
		return false;
	}
	UE_LOG(LogPPakPacher, Display, TEXT("CheckDiff Success."));

	// PatchDiff : 产出 PatchedFile，再对比 MD5
	if (!ResPatcher.PatchDiff(PatchedFile, InOldFile, PatchData))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("PatchDiff Failed."));
		return false;
	}

	const FMD5Hash NewHash = FMD5Hash::HashFile(*InNewFile);
	const FMD5Hash PatchedHash = FMD5Hash::HashFile(*PatchedFile);
	if (LexToString(NewHash) == LexToString(PatchedHash))
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

	FPResPatcher ResPatcher;

	FString PathPart, FilenamePart, ExtPart;
	FPaths::Split(InOldPakFile, PathPart, FilenamePart, ExtPart);
	FString PatchFileName = FPaths::Combine(PathPart, FilenamePart) + TEXT(".patch");

	// step 1: generate patch data.
	FPResPatchDataPtr PatchData;
	if (ResPatcher.CreateDiff(PatchFileName, InNewPakFile, InOldPakFile, PatchData))
	{
		UE_LOG(LogPPakPacher, Display, TEXT("CreateDiff Success."));
	}
	else
	{
		UE_LOG(LogPPakPacher, Error, TEXT("CreateDiff Failed."));
		return false;
	}

	// step 2: save patch data to file.
	if (PatchData.IsValid() && PatchData->IsUsePrecache())
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
	FPResPatchDataPtr NewPatchData = MakeShareable(new FPResPatchData());
	if (NewPatchData->LoadFromFile(PatchFileName))
	{
		UE_LOG(LogPPakPacher, Display, TEXT("LoadFromFile Success."));
	}
	else
	{
		UE_LOG(LogPPakPacher, Error, TEXT("LoadFromFile Failed."));
		return false;
	}

	if (PatchData.IsValid() && NewPatchData->IsEqual(*PatchData.Get()))
	{
		UE_LOG(LogPPakPacher, Display, TEXT("Check Save & Load Patch Data Success."));
	}
	else if (PatchData.IsValid())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("The loaded data is inconsistent with the saved data."));
		return false;
	}

	// write to tmp file for binary file diff. 
	//NewPatchData->SaveToFile(FPaths::Combine(PathPart, TEXT("tmp")) + TEXT(".patch"));

	// step 4: check patch.
	if (ResPatcher.CheckDiff(InNewPakFile, InOldPakFile, NewPatchData))
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
	if (ResPatcher.PatchDiff(PatchedPakFilename, InOldPakFile, NewPatchData))
	{
		UE_LOG(LogPPakPacher, Display, TEXT("Patch Pak Success."));
	}
	else
	{
		UE_LOG(LogPPakPacher, Error, TEXT("Patch Pak Failed."));
		return false;
	}

	// step 6: compare new pak file and patched pak files.
	bool bIsEqual = false;

	const FMD5Hash NewHash = FMD5Hash::HashFile(*InNewPakFile);
	const FMD5Hash PatchedHash = FMD5Hash::HashFile(*PatchedPakFilename);
	const FString NewMD5 = LexToString(NewHash);
	const FString PatchedMD5 = LexToString(PatchedHash);
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