#include "PPakPatcher.h"
#include "PakFileUtilities.h"
#include "PPakPatcherModule.h"
#include "Data/PPakPacherDataType.h"
#include "Data/PPakPatcherKeyChainHelper.h"
#include "Archive/PPakMemoryArchive.h"
#include "Archive/PSignedPakPatchWriter.h"
#include "Utils/PPakPatcherUtils.h"
#include "Utils/PDebugMemoryBank.h"
#include "PPakPatcherSettings.h"

//begin engine heads
#include "Misc/ICompressionFormat.h"
#include "Misc/KeyChainUtilities.h"
#include "Serialization/LargeMemoryWriter.h"

int64 CalcEntryRealSize(const FPakEntry& Entry, const FPakFile& PakFile)
{
	int64 RealSize = 0;
	bool bIsCompressed = Entry.CompressionMethodIndex != 0;

	if (bIsCompressed)
	{
		check(Entry.CompressionBlocks.Num() > 0);
		RealSize = Entry.GetSerializedSize(PakFile.GetInfo().Version);
		for (int32 i=0; i< Entry.CompressionBlocks.Num(); ++i)
		{
			
			int32 BlockSize = Entry.CompressionBlocks[i].CompressedEnd - Entry.CompressionBlocks[i].CompressedStart;
			RealSize += Entry.IsEncrypted() ? Align(BlockSize, FAES::AESBlockSize) : BlockSize;
		}
	}
	else
	{
		RealSize = Entry.GetSerializedSize(PakFile.GetInfo().Version);
		RealSize += Entry.IsEncrypted() ? Align(Entry.Size, FAES::AESBlockSize) : Entry.Size;
	}
	return RealSize;
}

bool FPPakPatcher::CreatePakDiff(const FString& InPatchFilename, const FPPakFileDataPtr& InNewPak, const FPPakFileDataPtr& InOldPak, FPPakPatchDataPtr& OutPatch)
{
	/*
			pak order:
				-> [padding + file entry + data] x n
				-> primary index
				-> path hash index
				-> full directory index
				-> head
		*/

	const double StartTime = FPlatformTime::Seconds();

	OutPatch = MakeShareable(new FPPakPatchData());

	OutPatch->BeginRecord(InPatchFilename, InNewPak, InOldPak);

	FPakFile& NewPakFile = *InNewPak->PakFilePtr;
	FArchive& NewPakReader = *NewPakFile.GetSharedReader(NULL);

	FPakFile& OldPakFile = *InOldPak->PakFilePtr;
	FArchive& OldPakReader = *OldPakFile.GetSharedReader(NULL);

	IPBinPatcher* BinPatcher = IPPakPatcherModule::Get().GetBinPatcher();

	FKeyChain& KeyChain = FPPakPatcherKeyChainHelper::Get().GetKeyChain();

	struct FPPakEntryInfo
	{
		FString Filename;
		FPakEntry Info;
	};

	TArray<FPPakEntryInfo> EntryInfos;
	for (FPakFile::FPakEntryIterator It(NewPakFile); It; ++It)
	{
		FPPakEntryInfo& Pair = EntryInfos.AddZeroed_GetRef();
		Pair.Filename = *It.TryGetFilename();
		Pair.Info = It.Info();
	}

	// Reorder to serialization order.
	EntryInfos.Sort([](const FPPakEntryInfo& A, const FPPakEntryInfo& B) {
		return A.Info.Offset < B.Info.Offset;
		});

	for (int32 i = 0; i < EntryInfos.Num(); ++i)
	{
		const FString& FileName = EntryInfos[i].Filename;
		const FPakEntry& NewEntry = EntryInfos[i].Info;
		const int64 NewRealSize = CalcEntryRealSize(NewEntry, NewPakFile);

		FString NewHash, OldHash;

		NewPakReader.Seek(NewEntry.Offset);

		if (FPPakPatcherSettings::Get().bDoubleCheckEntry)
		{
			//double check new entry info and move NewPakReader into place
			FPakEntry NewEntryInfo;
			NewEntryInfo.Serialize(NewPakReader, NewPakFile.GetInfo().Version);
			if (!NewEntryInfo.IndexDataEquals(NewEntry))
			{
				UE_LOG(LogPPakPacher, Error, TEXT("NewPakEntry double check failed. filename:%s"), *FileName);
				continue;
			}
			NewHash = BytesToHex(NewEntryInfo.Hash, sizeof(NewEntryInfo.Hash));
		}

		//see if entry exists in old pak							
		FPakEntry OldEntry;
		FPakFile::EFindResult FoundResult = OldPakFile.Find(OldPakFile.GetMountPoint() / FileName, &OldEntry);
		if (FoundResult == FPakFile::EFindResult::Found)
		{
			const int64 OldRealSize = CalcEntryRealSize(OldEntry, OldPakFile);
			OldPakReader.Seek(OldEntry.Offset);
			if (FPPakPatcherSettings::Get().bDoubleCheckEntry)
			{
				//double check old entry info and move OldPakReader into place
				FPakEntry OldEntryInfo;
				OldEntryInfo.Serialize(OldPakReader, OldPakFile.GetInfo().Version);
				if (!OldEntryInfo.IndexDataEquals(OldEntry))
				{
					UE_LOG(LogPPakPacher, Error, TEXT("OldPakEntry double check failed. filename:%s"), *FileName);
					continue;
				}
				OldHash = BytesToHex(OldEntryInfo.Hash, sizeof(OldEntryInfo.Hash));
			}

			FPPakMemoryArchive NewPakWriter(NewRealSize);
			NewPakReader.Seek(NewEntry.Offset);
			NewPakReader.Serialize(NewPakWriter.GetData(), NewRealSize);

			FPPakMemoryArchive OldPakWriter(OldRealSize);
			OldPakReader.Seek(OldEntry.Offset);
			OldPakReader.Serialize(OldPakWriter.GetData(), OldRealSize);

			if (NewHash == OldHash &&
				NewRealSize == OldRealSize &&
				FMemory::Memcmp(NewPakWriter.GetData(), OldPakWriter.GetData(), NewRealSize) == 0)
			{
				//record keep same
				OutPatch->RecordKeep(FileName, NewPakFile, OldPakFile, NewEntry, OldEntry, NewRealSize, OldRealSize);
			}
			else
			{
				//record modified
				TArray<uint8> PatchData;
				BinPatcher->CreateDiff(NewPakWriter.GetData(), NewPakWriter.GetSize(), OldPakWriter.GetData(), OldPakWriter.GetSize(), PatchData);
				bool bCheckSuccess = BinPatcher->CheckDiff(NewPakWriter.GetData(), NewPakWriter.GetSize(), OldPakWriter.GetData(), OldPakWriter.GetSize(), PatchData.GetData(), PatchData.Num());
				check(bCheckSuccess);
				FPPakFilePatchInfo& PatchInfo = OutPatch->RecordModify(FileName, NewPakFile, OldPakFile, NewEntry, OldEntry, PatchData, NewRealSize, OldRealSize);
			}
		}
		else
		{
			//record new file.
			FPPakMemoryArchive MemWriter(NewRealSize);
			NewPakReader.Seek(NewEntry.Offset);
			NewPakReader.Serialize(MemWriter.GetData(), NewRealSize);

			OutPatch->RecordNew(FileName, NewPakFile, OldPakFile, NewEntry, MemWriter, NewRealSize);
		}
	}

	const FPakInfo& NewPakInfo = NewPakFile.GetInfo();
	const FPakInfo& OldPakInfo = OldPakFile.GetInfo();

	auto RecordDataBlock = [&](FPPakPatchDataInfo& DataInfo, int64 NewOffset, int64 NewSize, int64 OldOffset, int64 OldSize, bool bIsPatchData)
	{
		FPPakMemoryArchive NewMemWriter(NewSize);
		NewPakReader.Seek(NewOffset);
		NewPakReader.Serialize(NewMemWriter.GetData(), NewSize);

		if (bIsPatchData)
		{
			FPPakMemoryArchive OldMemWriter(OldSize);
			OldPakReader.Seek(OldOffset);
			OldPakReader.Serialize(OldMemWriter.GetData(), OldSize);

			TArray<uint8> PatchData;
			BinPatcher->CreateDiff(NewMemWriter.GetData(), NewMemWriter.GetSize(), OldMemWriter.GetData(), OldMemWriter.GetSize(), PatchData);
			bool bCheckSuccess = BinPatcher->CheckDiff(NewMemWriter.GetData(), NewMemWriter.GetSize(), OldMemWriter.GetData(), OldMemWriter.GetSize(), PatchData.GetData(), PatchData.Num());
			check(bCheckSuccess);

			OutPatch->RecordDataBlock(DataInfo, NewOffset, NewSize, OldOffset, OldSize, PatchData.GetData(), PatchData.Num(), bIsPatchData);
		}
		else
		{
			OutPatch->RecordDataBlock(DataInfo, NewOffset, NewSize, OldOffset, OldSize, NewMemWriter.GetData(), NewMemWriter.GetSize(), bIsPatchData);
		}
	};

	// record new pak index block
	{
		bool bIsPatchData = FPPakPatcherSettings::Get().bBinaryPatchIndexBlock;

		int64 NewOffset = NewPakInfo.IndexOffset;
		int64 NewSize = NewPakInfo.IndexSize;
		int64 OldOffset = OldPakInfo.IndexOffset;
		int64 OldSize = OldPakInfo.IndexSize;

		RecordDataBlock(OutPatch->Info.IndexPatchInfo, NewOffset, NewSize, OldOffset, OldSize, bIsPatchData);
	}

	// record path hash & full directory index block
	{
		bool bIsPatchData = FPPakPatcherSettings::Get().bBinaryPatchPathBlock;

		int64 NewOffset = NewPakInfo.IndexOffset + NewPakInfo.IndexSize;
		int64 NewEnd = NewPakReader.TotalSize() - NewPakInfo.GetSerializedSize(NewPakInfo.Version) - 1;
		int64 NewSize = NewEnd - NewOffset + 1;

		int64 OldOffset = OldPakInfo.IndexOffset + OldPakInfo.IndexSize;
		int64 OldEnd = OldPakReader.TotalSize() - OldPakInfo.GetSerializedSize(OldPakInfo.Version) - 1;
		int64 OldSize = OldEnd - OldOffset + 1;

		RecordDataBlock(OutPatch->Info.PathPatchInfo, NewOffset, NewSize, OldOffset, OldSize, bIsPatchData);
	}

	// record head block
	{
		bool bIsPatchData = FPPakPatcherSettings::Get().bBinaryPatchHeadBlock;

		int64 NewSize = NewPakInfo.GetSerializedSize(NewPakInfo.Version);
		int64 NewOffset = NewPakReader.TotalSize() - NewSize;
		int64 OldSize = OldPakInfo.GetSerializedSize(OldPakInfo.Version);
		int64 OldOffset = OldPakReader.TotalSize() - OldSize;

		RecordDataBlock(OutPatch->Info.HeadPatchInfo, NewOffset, NewSize, OldOffset, OldSize, bIsPatchData);
	}

	if (InNewPak->bIsSigned)
	{
		if (FPPakPatcherSettings::Get().bUseSignWriter)
		{
			// Index hash use for SignedPakWriter
			OutPatch->Info.IndexHash = NewPakInfo.IndexHash;
			// TODO : currently missing private key
		}
		if (FPPakPatcherSettings::Get().bRecordSignToPatch)
		{
			FString SignFilename = FPaths::ChangeExtension(InNewPak->PakFilename, TEXT("sig"));
			FArchive* SignFileReader = IFileManager::Get().CreateFileReader(*SignFilename);
			if (SignFileReader)
			{
				int64 Size = SignFileReader->TotalSize();
				FPPakMemoryArchive MemWriter(Size);
				SignFileReader->Seek(0);
				SignFileReader->Serialize(MemWriter.GetData(), Size);

				OutPatch->RecordDataBlock(OutPatch->Info.SignFileInfo, 0, Size, 0, 0, MemWriter.GetData(), MemWriter.GetSize(), false);

				SignFileReader->Close();
				delete SignFileReader;
			}
			else
			{
				UE_LOG(LogPPakPacher, Error, TEXT("Create sign file reader failed. filename:%s"), *SignFilename);
			}
		}
	}
	

	OutPatch->EndRecord();

	UE_LOG(LogPPakPacher, Display, TEXT("Create pak patch successed. Cost time %.2lfs. Filename:%s"), FPlatformTime::Seconds() - StartTime, *InPatchFilename);

	return true;
}

FArchive* CreatePakPatchWriter(const TCHAR* Filename, const FKeyChain& InKeyChain, bool bSign)
{
	FArchive* Writer = IFileManager::Get().CreateFileWriter(Filename);
	if (Writer)
	{
		if (bSign && FPPakPatcherSettings::Get().bUseSignWriter)
		{
			UE_LOG(LogPPakPacher, Display, TEXT("Creating signed pak %s."), Filename);
			Writer = new FPSignedPakPatchWriter(*Writer, Filename, InKeyChain.SigningKey);
		}
		else
		{
			UE_LOG(LogPPakPacher, Display, TEXT("Creating pak %s."), Filename);
		}
	}

	return Writer;
}

bool FPPakPatcher::PatchPak(const FString& InNewPakFilename, const FPPakFileDataPtr& InOldPak, const FPPakPatchDataPtr& InPatch)
{
	const double StartTime = FPlatformTime::Seconds();

	FString NewPakFilename = InNewPakFilename;
	FPaths::MakeStandardFilename(NewPakFilename);
	if (NewPakFilename.IsEmpty())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::PatchPak - NewPakFilename was empty."));
		return false;
	}
	if (!InOldPak.IsValid())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::PatchPak - InOldPak was Invalid."));
		return false;
	}

	if (!InPatch.IsValid())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::PatchPak - InPatch was Invalid."));
		return false;
	}

	if(!InPatch->Info.OldPakHash.IsEmpty() &&
		InPatch->Info.OldPakHash != InOldPak->GetPakFileMD5())
	{
		UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::PatchPak - FileMD5 Dismatched. Pak Filename:%s, Pak MD5:%s, Record Filename:%s, Record MD5:%s"),
		*InOldPak->PakFilename, *InOldPak->GetPakFileMD5(), *InPatch->Info.OldPakName, *InPatch->Info.OldPakHash);
		return false;
	}

	IPBinPatcher* BinPatcher = IPPakPatcherModule::Get().GetBinPatcher();
	FPakFile& OldPakFile = *InOldPak->PakFilePtr;
	FArchive& OldPakReader = *OldPakFile.GetSharedReader(NULL);

	FKeyChain& KeyChain = FPPakPatcherKeyChainHelper::Get().GetKeyChain();
	bool bSign = InPatch->Info.bSign;
	TUniquePtr<FArchive> Writer(CreatePakPatchWriter(*NewPakFilename, KeyChain, bSign));

	if (!Writer)
	{
		UE_LOG(LogPPakPacher, Error, TEXT("Unable to create pak file \"%s\"."), *NewPakFilename);
		return false;
	}

	auto Write = [&](uint8* InData, int64 InSize)
	{
		Writer->Serialize(InData, InSize);
	};


	int64 PaddingBufferSize = 64 * 1024;
	FPPakPatcherMemory PaddingBuffer;
	auto SerializePaddingToOffset = [&](int64 InOffset){
		if (PaddingBuffer.GetSize() == 0)
		{
			PaddingBuffer.Resize(PaddingBufferSize);
			FMemory::Memset(PaddingBuffer.GetData(), 0, PaddingBufferSize);
		}
		int64 PaddingSize = InOffset - Writer->TotalSize();
		check(PaddingSize >= 0);
		check(PaddingSize <= PaddingBufferSize);
		if (PaddingSize > 0)
		{
			Write(PaddingBuffer.GetData(), PaddingSize);
		}
	};

	for(int32 i=0; i< InPatch->Info.FilePatchInfos.Num(); ++i)
	{
		FPPakFilePatchInfo& FilePatchInfo = InPatch->Info.FilePatchInfos[i];

		check(FilePatchInfo.DataInfo.NewOffset >= Writer->Tell());

		SerializePaddingToOffset(FilePatchInfo.DataInfo.NewOffset);
		FString Filename = FilePatchInfo.FileName;
		if (FilePatchInfo.PatchType == EPakFilePatchType::Keep || FilePatchInfo.PatchType == EPakFilePatchType::Modify)
		{
			//see if entry exists in old pak							
			FPakEntry OldEntry;
			FString OldHash;
			FPakFile::EFindResult FoundResult = OldPakFile.Find(OldPakFile.GetMountPoint() / FilePatchInfo.FileName, &OldEntry);
			if (FoundResult == FPakFile::EFindResult::Found)
			{
				OldPakReader.Seek(OldEntry.Offset);
				if (FPPakPatcherSettings::Get().bDoubleCheckEntry)
				{
					//double check old entry info and move OldPakReader into place
					FPakEntry OldEntryInfo;
					OldEntryInfo.Serialize(OldPakReader, OldPakFile.GetInfo().Version);
					if (!OldEntryInfo.IndexDataEquals(OldEntry))
					{
						UE_LOG(LogPPakPacher, Error, TEXT("OldPakEntry double check failed. filename:%s"), *Filename);
						continue;
					}
					OldHash = BytesToHex(OldEntryInfo.Hash, sizeof(OldEntryInfo.Hash));
				}
			}
			else
			{
				UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::PatchPak - Can't find pak file:[%s], in pak:[%s]"), *Filename, *InOldPak->PakFilename);
				return false;
			}

			int64 OldRealSize = FilePatchInfo.OldFileRealSize;
			FPPakMemoryArchive OldPakMemory(OldRealSize);
			OldPakReader.Seek(FilePatchInfo.DataInfo.OldOffset);
			OldPakReader.Serialize(OldPakMemory.GetData(), OldRealSize);

			if (FilePatchInfo.PatchType == EPakFilePatchType::Keep)
			{
				Write(OldPakMemory.GetData(), OldPakMemory.GetSize()); 
				continue;
			}
			else if (FilePatchInfo.PatchType == EPakFilePatchType::Modify)
			{
				FPPakMemoryArchive NewPakMemory(FilePatchInfo.FileRealSize);
				uint8* PatchData = InPatch->GetFilePatchData(FilePatchInfo.DataInfo);
				BinPatcher->Patch(NewPakMemory.GetData(), NewPakMemory.GetSize(),
					OldPakMemory.GetData(), OldPakMemory.GetSize(),
					PatchData, FilePatchInfo.DataInfo.DataSize);

				Write(NewPakMemory.GetData(), NewPakMemory.GetSize());
				continue;
			}
		}
		else if (FilePatchInfo.PatchType == EPakFilePatchType::New)
		{
			uint8* PatchData = InPatch->GetFilePatchData(FilePatchInfo.DataInfo);
			Write(PatchData, FilePatchInfo.DataInfo.DataSize);
		}
		else if (FilePatchInfo.PatchType == EPakFilePatchType::Delete)
		{
			//nothing to do.
		}
	}
	auto PatchDataBlock = [&](FPPakPatchDataInfo& Info)
	{
			SerializePaddingToOffset(Info.NewOffset);
			uint8* PatchData = InPatch->GetFilePatchData(Info);
			if (Info.bIsPatchData)
			{
				FPPakMemoryArchive OldPakMemory(Info.OldSize);
				OldPakReader.Seek(Info.OldOffset);
				OldPakReader.Serialize(OldPakMemory.GetData(), Info.OldSize);

				FPPakMemoryArchive NewPakMemory(Info.NewSize);

				BinPatcher->Patch(NewPakMemory.GetData(), NewPakMemory.GetSize(),
					OldPakMemory.GetData(), OldPakMemory.GetSize(),
					PatchData, Info.DataSize);

				Writer->Serialize(NewPakMemory.GetData(), NewPakMemory.GetSize());
			}
			else
			{
				Writer->Serialize(PatchData, Info.DataSize);
			}
	};
	// patch index block
	PatchDataBlock(InPatch->Info.IndexPatchInfo);
	
	// patch path block
	PatchDataBlock(InPatch->Info.PathPatchInfo);
	
	// patch head block
	PatchDataBlock(InPatch->Info.HeadPatchInfo);

	if (bSign)
	{
		if (FPPakPatcherSettings::Get().bUseSignWriter)
		{
			TArray<uint8> SignatureData;
			SignatureData.Append(InPatch->Info.IndexHash.Hash, UE_ARRAY_COUNT(FSHAHash::Hash));
			((FPSignedPakPatchWriter*)Writer.Get())->SetSignatureData(SignatureData);
		}
		if (FPPakPatcherSettings::Get().bRecordSignToPatch)
		{
			FPPakPatchDataInfo& Info = InPatch->Info.SignFileInfo;
			uint8* PatchData = InPatch->GetFilePatchData(Info);
			FString SignFilename = FPaths::ChangeExtension(NewPakFilename, FPPakPatcherSettings::Get().NewSignExtension);
			FArchive* SignFileWriter = IFileManager::Get().CreateFileWriter(*SignFilename);
			if (SignFileWriter)
			{
				SignFileWriter->Serialize(PatchData, Info.DataSize);
				SignFileWriter->Close();
				delete SignFileWriter;
			}
			else
			{
				UE_LOG(LogPPakPacher, Error, TEXT("Create sign file writer failed. filename:%s"), *SignFilename);
			}
		}
	}

	Writer->Close();

	UE_LOG(LogPPakPacher, Display, TEXT("Patch pak successed. Cost time %.2lfs. Filename:%s"), FPlatformTime::Seconds() - StartTime, *InNewPakFilename);

	return true;
}


bool FPPakPatcher::CheckPakDiff(const FPPakFileDataPtr& InNewPak, const FPPakFileDataPtr& InOldPak, const FPPakPatchDataPtr& InPatch)
{
	const double StartTime = FPlatformTime::Seconds();

	FPakFile& NewPakFile = *InNewPak->PakFilePtr;
	FArchive& NewPakReader = *NewPakFile.GetSharedReader(NULL);

	FPakFile& OldPakFile = *InOldPak->PakFilePtr;
	FArchive& OldPakReader = *OldPakFile.GetSharedReader(NULL);

	IPBinPatcher* BinPatcher = IPPakPatcherModule::Get().GetBinPatcher();

	int64 PaddingBufferSize = 64 * 1024;
	FPPakPatcherMemory PaddingBuffer;
	auto CheckReaderPaddingToOffset = [&](FArchive& Reader, int64 InToOffset) -> bool
		{
			if (bool bCheckReaderPadding = false)
			{
				int64 Size = InToOffset - Reader.Tell();
				if (Size == 0)
				{
					return true;
				}
				if (NewPakReader.Tell() + Size > Reader.TotalSize())
				{
					return false;
				}
				if (PaddingBuffer.GetSize() == 0)
				{
					PaddingBuffer.Resize(PaddingBufferSize);
					FMemory::Memset(PaddingBuffer.GetData(), 0, PaddingBufferSize);
				}
				FPPakPatcherMemory Memory;
				Memory.Resize(Size);
				Reader.Serialize(Memory.GetData(), Size);
				return FMemory::Memcmp(Memory.GetData(), PaddingBuffer.GetData(), Size) == 0;
			}
			return true;
		};

	FKeyChain& KeyChain = FPPakPatcherKeyChainHelper::Get().GetKeyChain();

	NewPakReader.Seek(0);

	for (FPPakFilePatchInfo& FilePatchInfo : InPatch->Info.FilePatchInfos)
	{
		// check padding.
		int64 StartOffset = NewPakReader.Tell();

		// debug.
		{
			NewPakReader.Seek(StartOffset);
		}
		if (!CheckReaderPaddingToOffset(NewPakReader, FilePatchInfo.DataInfo.NewOffset))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Check PakFile Padding Offset Failed. Offset:%lld, Size:%lld"), StartOffset, FilePatchInfo.DataInfo.NewOffset - StartOffset);
			return false;
		}

		const FString& Filename = FilePatchInfo.FileName;

		//see if entry exists in new pak							
		FPakEntry NewEntry;
		FString NewHash;
		{
			FPakFile::EFindResult FoundResult = NewPakFile.Find(NewPakFile.GetMountPoint() / Filename, &NewEntry);
			if (FoundResult == FPakFile::EFindResult::Found)
			{
				NewPakReader.Seek(NewEntry.Offset);
				if (FPPakPatcherSettings::Get().bDoubleCheckEntry)
				{
					//double check new entry info and move NewPakReader into place
					FPakEntry _NewEntry;
					_NewEntry.Serialize(NewPakReader, NewPakFile.GetInfo().Version);
					if (!_NewEntry.IndexDataEquals(NewEntry))
					{
						UE_LOG(LogPPakPacher, Error, TEXT("NewPakEntry double check failed. filename:%s, PakFilename:%s"), *Filename, *InNewPak->PakFilename);
						return false;
					}
					NewHash = BytesToHex(_NewEntry.Hash, sizeof(_NewEntry.Hash));
				}
			}
			else
			{
				UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Fild new pak file failed. filename:%s, PakFilename:%s"), *Filename, *InNewPak->PakFilename);
				return false;
			}
		}

		int64 NewRealSize = FilePatchInfo.FileRealSize;
		FPPakMemoryArchive NewPakMemory(NewRealSize);
		NewPakReader.Seek(NewEntry.Offset);
		NewPakReader.Serialize(NewPakMemory.GetData(), NewRealSize);

		if (FilePatchInfo.PatchType == EPakFilePatchType::Keep || FilePatchInfo.PatchType == EPakFilePatchType::Modify)
		{
			int64 OldRealSize = FilePatchInfo.OldFileRealSize;
			FPPakMemoryArchive OldPakMemory(OldRealSize);
			OldPakReader.Seek(FilePatchInfo.DataInfo.OldOffset);
			OldPakReader.Serialize(OldPakMemory.GetData(), OldRealSize);

			if (FilePatchInfo.PatchType == EPakFilePatchType::Keep)
			{
				if (NewPakMemory != OldPakMemory)
				{
					UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Compare pak file memory failed. filename:%s, PatchType:%s"),
						*Filename, *FPPakPatcherEnumHelper::ToString(FilePatchInfo.PatchType));
					return false;
				}
				continue;
			}
			else if (FilePatchInfo.PatchType == EPakFilePatchType::Modify)
			{
				FPPakMemoryArchive NewPakMemory2(NewRealSize);

				uint8* PatchData = InPatch->GetFilePatchData(FilePatchInfo.DataInfo);

				BinPatcher->Patch(NewPakMemory2.GetData(), NewPakMemory2.GetSize(),
					OldPakMemory.GetData(), OldPakMemory.GetSize(),
					PatchData, FilePatchInfo.DataInfo.DataSize);

				if (NewPakMemory != NewPakMemory2)
				{
					UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Compare pak file memory failed. filename:%s, PatchType:%s"),
						*Filename, *FPPakPatcherEnumHelper::ToString(FilePatchInfo.PatchType));
					return false;
				}
				continue;
			}
		}
		else if (FilePatchInfo.PatchType == EPakFilePatchType::New)
		{
			if (FilePatchInfo.DataInfo.DataSize != NewPakMemory.GetSize())
			{
				UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Compare pak file memory size not equal. filename:%s, PatchType:%s, NewSize:%lld, RecordSize:%lld"),
					*Filename, *FPPakPatcherEnumHelper::ToString(FilePatchInfo.PatchType), NewPakMemory.GetSize(), FilePatchInfo.DataInfo.DataSize);
				return false;	
			}
			uint8* PatchData = InPatch->GetFilePatchData(FilePatchInfo.DataInfo);
			if (FMemory::Memcmp(PatchData, NewPakMemory.GetData(), FilePatchInfo.DataInfo.DataSize) != 0)
			{
				UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Compare pak file memory failed. filename:%s, PatchType:%s"),
					*Filename, *FPPakPatcherEnumHelper::ToString(FilePatchInfo.PatchType));
				return false;
			}
			continue;
		}
	}

	auto CheckPatchData = [&](FPPakPatchDataInfo& Info)->bool
		{
			uint8* PatchData = InPatch->GetFilePatchData(Info);
			FPPakMemoryArchive NewPakMemory(Info.NewSize);
			NewPakReader.Seek(Info.NewOffset);
			NewPakReader.Serialize(NewPakMemory.GetData(), Info.NewSize);

			if (Info.bIsPatchData)
			{
				FPPakMemoryArchive OldPakMemory(Info.OldSize);
				OldPakReader.Seek(Info.OldOffset);
				OldPakReader.Serialize(OldPakMemory.GetData(), Info.OldSize);

				FPPakMemoryArchive PatchedMemory(Info.NewSize);
				BinPatcher->Patch(PatchedMemory.GetData(), PatchedMemory.GetSize(),
					OldPakMemory.GetData(), OldPakMemory.GetSize(),
					PatchData, Info.DataSize);

				if (NewPakMemory != PatchedMemory)
				{
					UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Patched block memory compare failed."));
					return false;
				}
			}
			else
			{
				if (FMemory::Memcmp(PatchData, NewPakMemory.GetData(), Info.NewSize) != 0)
				{
					UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - block memory compare failed."));
					return false;
				}
			}
			return true;
		};

	// check index block
	{
		FPPakPatchDataInfo& Info = InPatch->Info.IndexPatchInfo;
		int64 StartOffset = NewPakReader.Tell();
		if (!CheckReaderPaddingToOffset(NewPakReader, Info.NewOffset))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Check pak index block failed.. Offset:%lld, Size:%lld"), StartOffset, Info.NewOffset - StartOffset);
			return false;
		}
		const FPakInfo& NewPakInfo = NewPakFile.GetInfo();
		int64 Offset = NewPakInfo.IndexOffset;
		int64 Size = NewPakInfo.IndexSize;

		if (Offset != Info.NewOffset ||
			Size != Info.NewSize)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Index offset and size not equal. Pak Offset:%lld, Record Offset:%lld, Pak Size:%lld, Record Size:%lld"),
				Offset, Info.NewOffset, Size, Info.NewSize);
			return false;
		}

		if (!CheckPatchData(InPatch->Info.IndexPatchInfo))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Index block CheckPatchData failed."));
			return false;
		}
	}

	// check path block
	{
		FPPakPatchDataInfo& Info = InPatch->Info.PathPatchInfo;
		int64 StartOffset = NewPakReader.Tell();
		if (!CheckReaderPaddingToOffset(NewPakReader, Info.NewOffset))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Check pak index block failed.. Offset:%lld, Size:%lld"), StartOffset, Info.NewOffset - StartOffset);
			return false;
		}

		const FPakInfo& NewPakInfo = NewPakFile.GetInfo();
		int64 Offset = NewPakInfo.IndexOffset + NewPakInfo.IndexSize;
		int64 End = NewPakReader.TotalSize() - NewPakInfo.GetSerializedSize(NewPakInfo.Version) - 1;
		int64 Size = End - Offset + 1;

		if (Offset != Info.NewOffset ||
			Size != Info.NewSize)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Path block offset and size not equal. Pak Offset:%lld, Record Offset:%lld, Pak Size:%lld, Record Size:%lld"),
				Offset, Info.NewOffset, Size, Info.NewSize);
			return false;
		}

		if (!CheckPatchData(InPatch->Info.PathPatchInfo))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Path block CheckPatchData failed."));
			return false;
		}
	}

	// check head block
	{
		FPPakPatchDataInfo& Info = InPatch->Info.HeadPatchInfo;
		int64 StartOffset = NewPakReader.Tell();
		if (!CheckReaderPaddingToOffset(NewPakReader, Info.NewOffset))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Check pak index block failed.. Offset:%lld, Size:%lld"), StartOffset, Info.NewOffset - StartOffset);
			return false;
		}

		const FPakInfo& NewPakInfo = NewPakFile.GetInfo();
		int64 Size = NewPakInfo.GetSerializedSize(NewPakInfo.Version);
		int64 Offset = NewPakReader.TotalSize() - Size;

		if (Offset != Info.NewOffset ||
			Size != Info.NewSize)
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Head block offset and size not equal. Pak Offset:%lld, Record Offset:%lld, Pak Size:%lld, Record Size:%lld"),
				Offset, Info.NewOffset, Size, Info.NewSize);
			return false;
		}

		if (!CheckPatchData(InPatch->Info.HeadPatchInfo))
		{
			UE_LOG(LogPPakPacher, Error, TEXT("FPPakPatcher::CheckPakDiff - Head block CheckPatchData failed."));
			return false;
		}
	}

	UE_LOG(LogPPakPacher, Display, TEXT("Check pak patch successed. Cost time %.2lfs. Filename:%s"), FPlatformTime::Seconds() - StartTime, *InNewPak->PakFilename);

	return true;
}