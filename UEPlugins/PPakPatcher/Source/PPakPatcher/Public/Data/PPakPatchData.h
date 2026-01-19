#pragma once

#include "CoreMinimal.h"
#include "IPlatformFilePak.h"
#include "Templates/RefCounting.h"

#include "Data/PPakPacherDataType.h"
#include "Data/PPakFileData.h"
#include "Archive/PPakMemoryArchive.h"
#include "Archive/PPakPatcherMemory.h"


class PPAKPATCHER_API FPPakPatchDataInfo
{
public:
	bool bIsPatchData = false; // else full data.
	int64 NewOffset = 0;
	int64 NewSize = 0;
	int64 OldOffset = 0;
	int64 OldSize = 0;
	int64 DataOffset = 0;
	int64 DataSize = 0;

	void Serialize(FArchive& Ar);
	bool IsEqual(FPPakPatchDataInfo& Other);
};

class PPAKPATCHER_API FPPakFilePatchInfo
{
public:
	FString FileName;
	int64 FileUncompressedSize = 0;
	int64 FileRealSize = 0; // EntrySize + FileSize
	int64 OldFileRealSize = 0;
	EPakFilePatchType PatchType = EPakFilePatchType::Keep;
	FPPakPatchDataInfo DataInfo;

	FPakEntry Entry;

	void Serialize(FArchive& Ar);
	bool IsEqual(FPPakFilePatchInfo& Other);
};

class PPAKPATCHER_API FPPakPatchInfo
{
public:
	FString NewPakName;
	FString NewPakHash;
	int32 NewVersion;

	FString OldPakName;
	FString OldPakHash;
	int32 OldVersion;


	/*
		pak order:
			-> [padding + file entry + data] x n
			-> primary index
			-> path index (hash & full directory)
			-> head
	*/

	/* file entry + data record array */
	TArray<FPPakFilePatchInfo> FilePatchInfos;
	FPPakPatchDataInfo IndexPatchInfo;
	FPPakPatchDataInfo PathPatchInfo;
	FPPakPatchDataInfo HeadPatchInfo;

	FPPakPatchDataInfo SignFileInfo;

	FSHAHash IndexHash;

	bool bSign = false;
	FString MountPoint;

	void Serialize(FArchive& Ar);
	bool IsEqual(FPPakPatchInfo& Other);
};

class PPAKPATCHER_API FPPakPatchHead
{
public:
	int64 DataBlockOffset = 0;
	int64 DataBlockSize = 0;
	int64 IndexOffset = 0;
	int64 IndexSize = 0;

	void Serialize(FArchive& Ar);
	bool IsEqual(FPPakPatchHead& Other);
};

class PPAKPATCHER_API FPPakPatchData
{
public:
	FPPakPatchData();
	~FPPakPatchData();

	bool IsEqual(FPPakPatchData& Other);

	bool SaveToFile(const FString& InPatchFilename = TEXT(""));
	bool LoadFromFile(const FString& InPatchFilename);

	bool BeginRecord(const FString& InPatchFilename, const FPPakFileDataPtr& InNewPak, const FPPakFileDataPtr& InOldPak);

	FPPakFilePatchInfo& RecordKeep(const FString& InFileName, const FPakFile& NewPakFile, const FPakFile& OldPakFile,
								   const FPakEntry& NewEntry, const FPakEntry& OldEntry, int64 InNewRealSize, int64 InOldRealSize);

	FPPakFilePatchInfo& RecordModify(const FString& InFileName, const FPakFile& NewPakFile, const FPakFile& OldPakFile,
									 const FPakEntry& NewEntry, const FPakEntry& OldEntry, const TArray<uint8>& InPatchData,
									 int64 InNewRealSize, int64 InOldRealSize);

	FPPakFilePatchInfo& RecordNew(const FString& InFileName, const FPakFile& NewPakFile, const FPakFile& OldPakFile, const FPakEntry& NewEntry,
									 const FPPakMemoryArchive& InFileArchive, int64 InNewRealSize);

	void RecordIndexBlock(int64 InNewOffset, int64 InNewSize, int64 InOldOffset, int64 InOldSize, uint8* InData, int64 InDataSize, bool bIsPatchData);
	void RecordPathBlock(int64 InNewOffset, int64 InNewSize, int64 InOldOffset, int64 InOldSize, uint8* InData, int64 InDataSize, bool bIsPatchData);
	void RecordHeadBlock(int64 InNewOffset, int64 InNewSize, int64 InOldOffset, int64 InOldSize, uint8* InData, int64 InDataSize, bool bIsPatchData);
	void RecordDataBlock(FPPakPatchDataInfo& DataInfo, int64 InNewOffset, int64 InNewSize, int64 InOldOffset, int64 InOldSize, uint8* InData, int64 InDataSize, bool bIsPatchData);
	bool EndRecord();

	uint8* GetFilePatchData(FPPakPatchDataInfo& FilePatchInfo);
	bool GetFilePatchData(FPPakPatchDataInfo& FilePatchInfo, TArray<uint8>& OutCopy);
	bool IsUsePrecache(){return bUsePrecache;}
	FString PatchFilename;
	
	FPPakPatchInfo Info;
	FPPakPatchHead Head;
	int64 HeadOffset = 0;

private:
	bool SetupWriter(const FString& InFilename);
	bool SetupReader(const FString& InFilename);
	bool RecordData(const uint8* InSource, const int64 InSize, FPPakPatchDataInfo& OutDataInfo);
	bool FinalizeWriteFile();

	FArchive* Reader = nullptr;
	FArchive* Writer = nullptr;
	FPPakPatcherMemory Data;
	EPPatchDataSourceType DataSourceType = EPPatchDataSourceType::None;
	bool bUsePrecache = true;
};

typedef TSharedPtr<FPPakPatchData, ESPMode::ThreadSafe> FPPakPatchDataPtr;