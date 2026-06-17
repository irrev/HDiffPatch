#pragma once

#include "CoreMinimal.h"
#include "Serialization/Archive.h"
#include "Serialization/MemoryWriter.h"
#include "Math/BigInt.h"
#include "IPlatformFilePak.h"

/**
 * Wrapper for writing and signing an archive.
 *
 * 生命周期约定（修复 #36 复核）：
 *   1) 构造：用户传入 FArchive& InPak（通常是 IFileManager::Get().CreateFileWriter()），
 *      所有权由本类接管（析构时 `delete &PakWriter`）。
 *   2) 写入：Serialize() 累积到 Buffer；满 MaxChunkDataSize 时刷盘 + 累积 hash。
 *   3) **必须调 Close()**：除了刷剩余 buffer，还要写出 `.sig` 文件（含所有 chunk hash 的 RSA 签名）。
 *   4) 析构兜底：若调用方忘 Close()，析构会刷 buffer 并兜底写 sig 文件（带警告日志），
 *      避免半成品 pak 没 sig 文件被客户端拒绝。重复 Close 是 no-op。
 *   5) Seek 不支持（pak 流式写）。
 */
class FPSignedPakPatchWriter : public FArchive
{
	/** Buffer to sign */
	TArray<uint8> Buffer;
	/** Buffer writer */
	FMemoryWriter BufferArchive;
	/** The actual pak archive */
	FArchive& PakWriter;
	/** The filename of the signature file that accompanies the pak */
	FString PakSignaturesFilename;
	/** Size of the archive on disk (including signatures) */
	int64 SizeOnDisk;
	/** Data size (excluding signatures) */
	int64 NewSize;
	/** Signing key */
	const FRSAKeyHandle SigningKey;
	/** Hashes */
	TArray<TPakChunkHash> ChunkHashes;
	/** Signature data */
	TArray<uint8> SignatureData;
	/** 是否已成功完成（Close 或析构兜底）；防止重复写 sig 文件。 */
	bool bFinalized = false;

	/**
	 * Serializes and signs a buffer
	 */
	void SerializeBufferAndSign();

	/** 实际写出签名文件（仅在 bFinalized=false 时执行；执行后置 true）。 */
	void FinalizeSignatureFile();

public:

	FPSignedPakPatchWriter(FArchive& InPak, const FString& InPakFilename, const FRSAKeyHandle InSigningKey);
	virtual ~FPSignedPakPatchWriter();

	// FArchive interface
	virtual bool Close() override;
	virtual void Serialize(void* Data, int64 Length) override;
	virtual int64 Tell() override;
	virtual int64 TotalSize() override;
	virtual void Seek(int64 InPos) override;

	void SetSignatureData(TArray<uint8>& InSignatureData)
	{
		SignatureData = MoveTemp(InSignatureData);
	}
};
