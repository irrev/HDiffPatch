
#include "Archive/PSignedPakPatchWriter.h"
#include "IPlatformFilePak.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "Data/PPakPatcherDataType.h"
#include "PPakPatcherSettings.h"

FPSignedPakPatchWriter::FPSignedPakPatchWriter(FArchive& InPak, const FString& InPakFilename, const FRSAKeyHandle InSigningKey)
	: FArchive(InPak)
	, BufferArchive(Buffer)
	, PakWriter(InPak)
	, PakSignaturesFilename(FPaths::ChangeExtension(InPakFilename, UPPakPatcherSettings::Get().NewSignExtension))
	, SizeOnDisk(0)
	, NewSize(0)
	, SigningKey(InSigningKey)
{
	check(IsSaving());
	Buffer.Reserve(FPakInfo::MaxChunkDataSize);
}

FPSignedPakPatchWriter::~FPSignedPakPatchWriter()
{
	// 兜底：刷剩余 buffer + 写 sig 文件。如果用户已正确调 Close()，bFinalized=true 这里 no-op。
	if (BufferArchive.Tell() > 0)
	{
		SerializeBufferAndSign();
	}
	if (!bFinalized)
	{
		UE_LOG(LogPPakPacher, Warning,
			TEXT("FPSignedPakPatchWriter::~ - Close() was not called explicitly; writing signature file in destructor. File:%s"),
			*PakSignaturesFilename);
		FinalizeSignatureFile();
	}
	delete& PakWriter;
}

void FPSignedPakPatchWriter::SerializeBufferAndSign()
{
	// Compute a hash for this buffer data
	ChunkHashes.Add(ComputePakChunkHash(&Buffer[0], Buffer.Num()));

	// Flush the buffer
	PakWriter.Serialize(&Buffer[0], Buffer.Num());
	BufferArchive.Seek(0);
	Buffer.Empty(FPakInfo::MaxChunkDataSize);
}

void FPSignedPakPatchWriter::FinalizeSignatureFile()
{
	if (bFinalized)
	{
		return;
	}
	bFinalized = true;

	TUniquePtr<FArchive> SignatureWriter(IFileManager::Get().CreateFileWriter(*PakSignaturesFilename));
	if (!SignatureWriter)
	{
		UE_LOG(LogPPakPacher, Error,
			TEXT("FPSignedPakPatchWriter::FinalizeSignatureFile - failed to create signature file writer: %s"),
			*PakSignaturesFilename);
		return;
	}
	FPakSignatureFile SignatureFile;
	SignatureFile.SetChunkHashesAndSign(ChunkHashes, SignatureData, SigningKey);
	SignatureFile.Serialize(*SignatureWriter);
	// TUniquePtr 析构自动 Close 并 delete
}

bool FPSignedPakPatchWriter::Close()
{
	if (BufferArchive.Tell() > 0)
	{
		SerializeBufferAndSign();
	}

	// 写 sig 文件（重复 Close 是 no-op）
	FinalizeSignatureFile();

	return FArchive::Close();
}

void FPSignedPakPatchWriter::Serialize(void* Data, int64 Length)
{
	// Serialize data to a buffer. When the max buffer size is reached, the buffer is signed and
	// serialized to disk with its signature
	uint8* DataToWrite = (uint8*)Data;
	int64 RemainingSize = Length;
	while (RemainingSize > 0)
	{
		int64 BufferPos = BufferArchive.Tell();
		int64 SizeToWrite = RemainingSize;
		if (BufferPos + SizeToWrite > FPakInfo::MaxChunkDataSize)
		{
			SizeToWrite = FPakInfo::MaxChunkDataSize - BufferPos;
		}

		BufferArchive.Serialize(DataToWrite, SizeToWrite);
		if (BufferArchive.Tell() == FPakInfo::MaxChunkDataSize)
		{
			SerializeBufferAndSign();
		}

		SizeOnDisk += SizeToWrite;
		NewSize += SizeToWrite;

		RemainingSize -= SizeToWrite;
		DataToWrite += SizeToWrite;
	}
}

int64 FPSignedPakPatchWriter::Tell()
{
	return NewSize;
}

int64 FPSignedPakPatchWriter::TotalSize()
{
	return NewSize;
}

void FPSignedPakPatchWriter::Seek(int64 InPos)
{
	UE_LOG(LogPPakPacher, Fatal, TEXT("Seek is not supported in FPSignedPakPatchWriter."));
}