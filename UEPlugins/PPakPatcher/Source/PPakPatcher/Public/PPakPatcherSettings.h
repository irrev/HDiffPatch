#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Data/PPakPatcherDataType.h"
#include "PPakPatcherSettings.generated.h"


UCLASS(config=PPakPatcher, defaultconfig)
class PPAKPATCHER_API UPPakPatcherSettings : public UObject
{
	GENERATED_UCLASS_BODY()
public:
	UPPakPatcherSettings(){}
	static UPPakPatcherSettings& Get();

	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	EPPakPatchMode PakPatchMode = EPPakPatchMode::PakAware;

	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	FString NewSignExtension = TEXT(".newsig");

	// pre-cache patch data to memory when loading.
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bPrecachePatchDataOnLoad = false;

	// pre-cache patch data to memory when saving.
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bPrecachePatchDataOnSave = false;

	// double check pak entry post load.
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bDoubleCheckEntry = true;

	// if record index block with binary patch.
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bBinaryPatchIndexBlock = true;

	// if record path hash index block with binary patch (pak v10+).
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bBinaryPatchPathHashBlock = true;

	// if record full directory index block with binary patch (pak v10+).
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bBinaryPatchFullDirectoryBlock = true;

	// if record head block with binary patch.
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bBinaryPatchHeadBlock = true;

	// 当前运行时无法获取RSA的私密，导致无法正确写出来sig.
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bUseSignWriter = false;

	// serialize .sign file to patch file.
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bRecordSignToPatch = true;

	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bGenPakFileMD5 = false;

	// 校验文件hash的算法
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	EPPakCheckFileHashType CheckFileHashType = EPPakCheckFileHashType::Crc32;

	// -----------------------------------------------------------------------
	// PakAware 模式下，对单个 entry 字节流的预处理策略
	// -----------------------------------------------------------------------
	// 决定 diff 输入处于哪个层级（详见 EPPakAwarePreprocess 注释）：
	//   NoDecrypt              : 直接对密文+压缩字节做 HDiff
	//   DecryptAndCompress     : 解密 → 在压缩字节上做 HDiff
	//   DecryptAndDecompress   : 解密 + 解压 → 在原始字节上做 HDiff
	// 该值会被持久化到补丁文件 Header 中；运行时打补丁阶段必须按相同策略反向重组。
	// - NoDecrypt            : 已实装（默认）
	// - DecryptAndCompress   : 已实装（解密后在压缩字节上做 diff，运行时重新加密）
	// - DecryptAndDecompress : 未实装，开启会 warning 并回退到 NoDecrypt
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	EPPakAwarePreprocess PakAwarePreprocess = EPPakAwarePreprocess::NoDecrypt;

	//--------- HDiff Settings begin ---------
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bUseSingleCompressMode = true;

	// default 6, bin: 0--4  text: 4--9
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	int32 MinSingleMatchScore = 6;

	// big cache max used O(oldSize) memory, match speed faster, but build big cache slow 
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bUseBigCacheMatch = false;

	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	int32 ThreadNum = 1;

	//patchStepMemSize: default 256k, recommended 64k,2m etc...
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	int32 PatchStepMemSize = 262144;// 1024 * 256;
	//--------- HDiff Settings end -----------
};

