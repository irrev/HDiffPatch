#include "Utils/PPakPatcherUtils.h"
#include "Data/PPakPatcherDataType.h"

#include "PPakPatcherModule.h"
#include "PPakPatcherSettings.h"

#include "HAL/FileManager.h"

bool FPPakPatcherUtils::LoadFileToBuffer(const FString& InFileName, TArray<uint8>& OutBuffer)
{
	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*InFileName, EFileRead::FILEREAD_None));
	if (!Reader)
	{
		return false;
	}

	OutBuffer.Empty();
	const int64 Size = Reader->TotalSize();
	if (Size == 0)
	{
		return true;
	}

	OutBuffer.SetNumZeroed(Size);
	Reader->Serialize(OutBuffer.GetData(), Size);
	return Reader->Close();
}

bool FPPakPatcherUtils::LoadFileToBuffer(const FString& InFileName, TArray64<uint8>& OutBuffer)
{
	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*InFileName, EFileRead::FILEREAD_None));
	if (!Reader)
	{
		return false;
	}

	OutBuffer.Empty();
	const int64 Size = Reader->TotalSize();
	if (Size == 0)
	{
		return true;
	}

	OutBuffer.SetNumZeroed(Size);
	Reader->Serialize(OutBuffer.GetData(), Size);
	return Reader->Close();
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

int32 FPPakPatcherUtils::CalculateFileCrc32(const FString& InFilename, TArray<uint8>& Buffer)
{
	if (Buffer.IsEmpty())
	{
		Buffer.SetNumUninitialized(1024 * 64);
	}

	uint32 FileCRC = 0;
	TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileReader(*InFilename));
	if (Ar)
	{
		const int64 Size = Ar->TotalSize();
		int64 Position = 0;

		while (Position < Size)
		{
			const auto ReadNum = FMath::Min(Size - Position, (int64)Buffer.Num());
			Ar->Serialize(Buffer.GetData(), ReadNum);
			FileCRC = FCrc::MemCrc32(Buffer.GetData(), ReadNum, FileCRC);
			Position += ReadNum;
		}
	}
	return FileCRC;
}

FMD5Hash FPPakPatcherUtils::CalculateFileMD5(const FString& InFilename, TArray<uint8>& Buffer)
{
	return FMD5Hash::HashFile(*InFilename, &Buffer);
}

FString FPPakPatcherUtils::CalculateFileMD5String(const FString& InFilename)
{
	if (InFilename.IsEmpty() || !IFileManager::Get().FileExists(*InFilename))
	{
		return FString();
	}
	return LexToString(FMD5Hash::HashFile(*InFilename));
}

uint32 FPPakPatcherUtils::CalculateFileCrc32(const FString& InFilename)
{
	if (InFilename.IsEmpty() || !IFileManager::Get().FileExists(*InFilename))
	{
		return 0;
	}
	TArray<uint8> Buffer;
	return static_cast<uint32>(CalculateFileCrc32(InFilename, Buffer));
}

bool FPPakPatcherUtils::CalculateFileHashesAndSize(const FString& InFilename,
	FString& OutMD5, uint32& OutCRC32, int64& OutSize)
{
	OutMD5.Reset();
	OutCRC32 = 0;
	OutSize = 0;

	if (InFilename.IsEmpty() || !IFileManager::Get().FileExists(*InFilename))
	{
		return false;
	}

	TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileReader(*InFilename));
	if (!Ar)
	{
		return false;
	}

	const int64 Size = Ar->TotalSize();
	OutSize = Size;
	if (Size == 0)
	{
		// 空文件：MD5 / CRC32 都按"无内容"算（FMD5 finalize 给定空数据有标准结果，CRC32=0）
		FMD5 MD5;
		FMD5Hash MD5Result;
		MD5Result.Set(MD5);
		OutMD5 = LexToString(MD5Result);
		OutCRC32 = 0;
		return Ar->Close();
	}

	// 单 pass：同时 feed MD5 + CRC32
	FMD5 MD5;
	uint32 Crc = 0;
	TArray<uint8> Buffer;
	Buffer.SetNumUninitialized(64 * 1024);

	int64 Position = 0;
	while (Position < Size)
	{
		const int64 ReadNum = FMath::Min(Size - Position, (int64)Buffer.Num());
		Ar->Serialize(Buffer.GetData(), ReadNum);
		MD5.Update(Buffer.GetData(), ReadNum);
		Crc = FCrc::MemCrc32(Buffer.GetData(), ReadNum, Crc);
		Position += ReadNum;
	}

	FMD5Hash MD5Result;
	MD5Result.Set(MD5);
	OutMD5 = LexToString(MD5Result);
	OutCRC32 = Crc;
	return Ar->Close();
}

bool FPPakPatcherUtils::VerifyFileHashByCheckType(const FString& InFilename,
	const FString& ExpectedMD5, uint32 ExpectedCrc32,
	const TCHAR* InContextTagForLog)
{
	const TCHAR* Tag = InContextTagForLog ? InContextTagForLog : TEXT("VerifyFileHash");
	const EPPakCheckFileHashType HashType = UPPakPatcherSettings::Get().CheckFileHashType;

	switch (HashType)
	{
	case EPPakCheckFileHashType::None:
		return true;

	case EPPakCheckFileHashType::Crc32:
	{
		// 期望 CRC32 缺失（如旧补丁未带）则跳过
		if (ExpectedCrc32 == 0)
		{
			return true;
		}
		const uint32 ActualCrc32 = CalculateFileCrc32(InFilename);
		if (ActualCrc32 != ExpectedCrc32)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("[%s] File CRC32 mismatch. Expect:0x%08X Actual:0x%08X File:%s"),
				Tag, ExpectedCrc32, ActualCrc32, *InFilename);
			return false;
		}
		return true;
	}

	case EPPakCheckFileHashType::MD5:
	{
		if (ExpectedMD5.IsEmpty())
		{
			return true;
		}
		const FString ActualMD5 = CalculateFileMD5String(InFilename);
		if (ActualMD5.IsEmpty() || ActualMD5 != ExpectedMD5)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("[%s] File MD5 mismatch. Expect:%s Actual:%s File:%s"),
				Tag, *ExpectedMD5, *ActualMD5, *InFilename);
			return false;
		}
		return true;
	}

	default:
		return true;
	}
}

int64 FPPakPatcherUtils::GetFileSize(const FString& InFilename)
{
	return IFileManager::Get().FileSize(*InFilename);
}


FPFileCompareInfo FPPakPatcherUtils::CompareFile(const FString& InNewFile, const FString& InOldFile, TArray<uint8>& Buffer, bool bCompareMD5, bool bFastCompare)
{
	FPFileCompareInfo Result;
	bool bNewExists = !InNewFile.IsEmpty() && IFileManager::Get().FileExists(*InNewFile);
	bool bOldExists = !InOldFile.IsEmpty() && IFileManager::Get().FileExists(*InOldFile);

	Result.Filename = FPaths::GetCleanFilename(InNewFile);
	if (bNewExists && !bOldExists)
	{
		Result.NewFullPath = InNewFile;
		Result.NewSize = FPPakPatcherUtils::GetFileSize(InNewFile);
		Result.DiffType = EPFileCompareDiffType::Add;
	}
	else if (!bNewExists && bOldExists)
	{
		Result.OldFullPath = InOldFile;
		Result.OldSize = FPPakPatcherUtils::GetFileSize(InOldFile);
		Result.DiffType = EPFileCompareDiffType::Delete;
	}
	else if (bNewExists && bOldExists)
	{
		Result.NewFullPath = InNewFile;
		Result.NewSize = FPPakPatcherUtils::GetFileSize(InNewFile);
		Result.OldFullPath = InOldFile;
		Result.OldSize = FPPakPatcherUtils::GetFileSize(InOldFile);

		if (bFastCompare)
		{
			if (Result.NewSize == Result.OldSize)
			{
				Result.NewCrc = FPPakPatcherUtils::CalculateFileCrc32(InNewFile, Buffer);
				Result.OldCrc = FPPakPatcherUtils::CalculateFileCrc32(InOldFile, Buffer);
				if (Result.NewCrc == Result.OldCrc)
				{
					Result.NewMd5 = bCompareMD5 ? LexToString(FPPakPatcherUtils::CalculateFileMD5(InNewFile, Buffer)) : TEXT("");
					Result.OldMd5 = bCompareMD5 ? LexToString(FPPakPatcherUtils::CalculateFileMD5(InOldFile, Buffer)) : TEXT("");
				}
			}
		}
		else
		{
			Result.NewCrc = FPPakPatcherUtils::CalculateFileCrc32(InNewFile, Buffer);
			Result.OldCrc = FPPakPatcherUtils::CalculateFileCrc32(InOldFile, Buffer);
			Result.NewMd5 = bCompareMD5 ? LexToString(FPPakPatcherUtils::CalculateFileMD5(InNewFile, Buffer)) : TEXT("");
			Result.OldMd5 = bCompareMD5 ? LexToString(FPPakPatcherUtils::CalculateFileMD5(InOldFile, Buffer)) : TEXT("");
		}

		if (Result.NewSize == Result.OldSize && Result.NewCrc == Result.OldCrc && Result.NewMd5 == Result.OldMd5)
		{
			Result.DiffType = EPFileCompareDiffType::Equal;
		}
		else
		{
			Result.DiffType = EPFileCompareDiffType::Modify;
		}
	}

	return Result;
}

TArray<FPFileCompareInfo> FPPakPatcherUtils::CompareDirectories(const FString& InNewDir, const FString& InOldDir, bool bFastCompare)
{
	TArray<FPFileCompareInfo> Result;

	if (!IFileManager::Get().DirectoryExists(*InNewDir))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("New directory %s does not exist"), *InNewDir);
		return Result;
	}
	if (!IFileManager::Get().DirectoryExists(*InOldDir))
	{
		UE_LOG(LogPPakPacher, Error, TEXT("Old directory %s does not exist"), *InOldDir);
		return Result;
	}

	TArray<FString> NewFiles;
	IFileManager::Get().FindFilesRecursive(NewFiles, *InNewDir, TEXT("*"), true, false);

	TArray<FString> OldFiles;
	IFileManager::Get().FindFilesRecursive(OldFiles, *InOldDir, TEXT("*"), true, false);

	TArray<FString> AllRelPaths;

	for (const FString& File : NewFiles)
	{
		FString RelPath = File;
		FString Dir = InNewDir;
		Dir.ReplaceInline(TEXT("\\"), TEXT("/"));
		if(!Dir.EndsWith("/"))
		{
			Dir += "/";
		}
        FPaths::MakePathRelativeTo(RelPath, *Dir);
		AllRelPaths.AddUnique(RelPath);
	}
	for (const FString& File : OldFiles)
	{
		FString RelPath = File;
		FString Dir = InOldDir;
		Dir.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (!Dir.EndsWith("/"))
		{
			Dir += "/";
		}
		FPaths::MakePathRelativeTo(RelPath, *Dir);
		AllRelPaths.AddUnique(RelPath);
	}

	UE_LOG(LogPPakPacher, Display, TEXT("Begin Compare Files: %d"), AllRelPaths.Num());
	TArray<uint8> Buffer;
	for(int32 i=0; i<AllRelPaths.Num(); i++)
	{
		FString RelPath = AllRelPaths[i];
		FString NewPath = FPaths::Combine(InNewDir, RelPath);
		FString OldPath = FPaths::Combine(InOldDir, RelPath);

		FPFileCompareInfo Info = CompareFile(NewPath, OldPath, Buffer);
		Info.Filename = RelPath;
		Result.Add(Info);
        UE_LOG(LogPPakPacher, Display, TEXT("Compare[%d/%d]: %s (%s)"), i, AllRelPaths.Num(), *RelPath, *UEnum::GetValueAsString(Info.DiffType));
	}

	return Result;
}