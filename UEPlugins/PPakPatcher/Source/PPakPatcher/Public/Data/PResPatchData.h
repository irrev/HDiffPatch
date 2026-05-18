#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"

#include "Data/PPakPatcherDataType.h"
#include "Data/PPakFileData.h"
#include "Data/PPakPatchBody.h"
#include "Data/PBinPatchBody.h"
#include "Data/PIoStorePatchBody.h"
#include "Archive/PPakMemoryArchive.h"
#include "Archive/PPakPatcherMemory.h"


/**
 * 资源补丁类型。决定 FPResPatchData 携带哪个 Body。
 */
enum class EPResPatchType : uint8
{
	None    = 0,
	Bin     = 1,    // 任意二进制文件（非 pak/utoc/ucas）
	Pak     = 2,    // UE .pak 文件
	IoStore = 3,    // UE .utoc + .ucas 容器
};

/** FPResPatchData 的序列化格式版本号；新增字段/字段重排时递增。
 *  v3 (2026-05-15)：Pak 类型尾部追加 bHasIoStoreCompanion 标志位 + 可选 IoStoreBody，修复 IoStoreBody 之前未被序列化的致命 bug。
 *  v2：FPResPatchHeader 扩展 OldVersion/NewVersion/OldSize/NewSize/GenerateMode/PrincipalEncryptionKeyGuid 字段。
 *  v1：初始版本。
 */
static constexpr int32 PRES_PATCH_FORMAT_VERSION = 3;

/**
 * 补丁文件尾部索引段偏移记录（物理布局）。
 */
class PPAKPATCHER_API FPResPatchHead
{
public:
	int64 DataBlockOffset = 0;
	int64 DataBlockSize   = 0;
	int64 IndexOffset     = 0;   // Header + Body 序列化的起点
	int64 IndexSize       = 0;

	void Serialize(FArchive& Ar);
	bool IsEqual(FPResPatchHead& Other);
};

/**
 * 公共补丁头。三类补丁都必然有的字段。
 *
 * 文件 hash 双轨（MD5 + Crc32）：
 *   - 构建机生成补丁时：始终同时写入 MD5 和 Crc32（不受运行时 CheckFileHashType 影响）
 *   - 运行时校验时：根据 UPPakPatcherSettings::CheckFileHashType 选择校验哪个（None/Crc32/MD5）
 *
 * PakAwarePreprocess：仅对 Type=Pak 有意义；记录构建侧 entry 字节流的预处理层级，
 * 运行时打补丁必须按相同策略反向重组。Type!=Pak 时保持 NoDecrypt 即可。
 *
 * 文件元数据（OldVersion/NewVersion/OldSize/NewSize）：
 *   - OldVersion/NewVersion：Type=Pak 时为 FPakInfo::Version；Type=Bin/IoStore 时为 0。
 *   - OldSize/NewSize：旧/新文件字节数；构建侧填充，运行时可用于"小文件快速错位检测"。
 *
 * GenerateMode：生成端使用的模式（PakAware/Binary）。运行端无需指定，自动从此读取分发。
 *
 * PrincipalEncryptionKeyGuid：仅对加密 Type=Pak 有意义；记录构建侧主加密 key 的 Guid，
 *   运行时若全局 KeyChain 中没有同名 key 可立刻报错（而不是在 Index 解密时崩溃）。
 *   非加密 pak 留空 Guid。
 */
class PPAKPATCHER_API FPResPatchHeader
{
public:
	EPResPatchType Type = EPResPatchType::None;
	int32 FormatVersion = PRES_PATCH_FORMAT_VERSION;

	FString OldFileName;
	FString NewFileName;

	FString OldMD5;
	FString NewMD5;

	uint32 OldCrc32 = 0;
	uint32 NewCrc32 = 0;

	/** 文件元数据：Pak 版本号；非 Pak 类型为 0。 */
	int32 OldVersion = 0;
	int32 NewVersion = 0;

	/** 文件元数据：旧/新文件字节数。 */
	int64 OldSize = 0;
	int64 NewSize = 0;

	EPakPatchCompressType CompressType = EPakPatchCompressType::None;

	/** PakAware 模式下 entry 字节流预处理层级（与生成侧 UPPakPatcherSettings::PakAwarePreprocess 一致）。 */
	EPPakAwarePreprocess PakAwarePreprocess = EPPakAwarePreprocess::NoDecrypt;

	/** 生成端模式（PakAware/Binary）。运行端按 Type 分发，但此字段保留作为审计/诊断信息。 */
	EPPakPatchMode GenerateMode = EPPakPatchMode::PakAware;

	/** 主加密 key 的 Guid（FGuid 序列化为 16 字节）；非加密 pak 留空 Guid。 */
	FGuid PrincipalEncryptionKeyGuid;

	void Serialize(FArchive& Ar);
	bool IsEqual(FPResPatchHeader& Other);
};

/**
 * 资源补丁统一容器。公共头 + 按 Type 激活的专属 Body + 底层字节池。
 *
 * 物理文件布局：
 *   [DataBlock]    承载 Bin/Pak/IoStore 所有 HDiff 或 full data 的字节池
 *   [Index]        FPResPatchHeader + Body 的序列化字节
 *   [Head]         FPResPatchHead
 *   [HeadOffset]   8 字节尾部指针
 */
class PPAKPATCHER_API FPResPatchData
{
public:
	FPResPatchData();
	~FPResPatchData();

	bool IsEqual(FPResPatchData& Other);

	bool SaveToFile(const FString& InPatchFilename = TEXT(""));
	bool LoadFromFile(const FString& InPatchFilename);

	/**
	 * 初始化一次记录流程。根据 Type 自动创建对应 Body。
	 * 调用方之后可通过 GetPakBody() / GetBinBody() / GetIoStoreBody() 取 body 指针填充内容。
	 *
	 * 注意：构建侧应**同时**传入 MD5 与 CRC32，运行时根据 CheckFileHashType 选择校验。
	 */
	bool BeginRecord(const FString& InPatchFilename, EPResPatchType InType,
		const FString& InOldFileName, const FString& InNewFileName,
		const FString& InOldMD5, const FString& InNewMD5,
		uint32 InOldCrc32, uint32 InNewCrc32,
		EPakPatchCompressType InCompressType);

	bool EndRecord();

	/**
	 * 中止当前记录流程：关闭 Writer、删除半成品 patch 文件（仅 bPrecachePatchDataOnSave=false 模式下需要）。
	 * 用于 Modify/IoStore 联动等中途失败时清理脏文件。失败处理路径上调用，永不抛错。
	 */
	void AbortRecord();

	// ---- Pak 专用便捷 API（委托到 PakBody） ----
	FPPakFilePatchInfo& RecordKeep(const FString& InFileName, const FPakFile& NewPakFile, const FPakFile& OldPakFile,
								   const FPakEntry& NewEntry, const FPakEntry& OldEntry, int64 InNewRealSize, int64 InOldRealSize);

	FPPakFilePatchInfo& RecordModify(const FString& InFileName, const FPakFile& NewPakFile, const FPakFile& OldPakFile,
									 const FPakEntry& NewEntry, const FPakEntry& OldEntry, const TArray<uint8>& InPatchData,
									 int64 InNewRealSize, int64 InOldRealSize);

	FPPakFilePatchInfo& RecordNew(const FString& InFileName, const FPakFile& NewPakFile, const FPakFile& OldPakFile, const FPakEntry& NewEntry,
								  const FPPakMemoryArchive& InFileArchive, int64 InNewRealSize);

	// ---- 通用：记录任意 DataBlock（Primary/PathHash/FullDirectory/Head/Sign/Bin Diff/IoStore Utoc/Ucas 等均用此） ----
	void RecordDataBlock(FPPakPatchDataInfo& DataInfo, int64 InNewOffset, int64 InNewSize, int64 InOldOffset, int64 InOldSize,
	                     uint8* InData, int64 InDataSize, bool bIsPatchData);

	// ---- 读取接口 ----
	uint8* GetFilePatchData(FPPakPatchDataInfo& FilePatchInfo);
	bool GetFilePatchData(FPPakPatchDataInfo& FilePatchInfo, TArray<uint8>& OutCopy);

	bool IsUsePrecache() const { return bUsePrecache; }

	// ---- Body 访问器 ----
	FPPakPatchBody*      GetPakBody()      { return PakBody.Get(); }
	FPBinPatchBody*      GetBinBody()      { return BinBody.Get(); }
	FPIoStorePatchBody*  GetIoStoreBody()  { return IoStoreBody.Get(); }
	const FPPakPatchBody*      GetPakBody() const     { return PakBody.Get(); }
	const FPBinPatchBody*      GetBinBody() const     { return BinBody.Get(); }
	const FPIoStorePatchBody*  GetIoStoreBody() const { return IoStoreBody.Get(); }

	// ---- 公有成员（记录流程中需直接读写） ----
	FString PatchFilename;
	FPResPatchHeader Header;
	FPResPatchHead   Head;
	int64 HeadOffset = 0;

	/**
	 * Body 持有规则（v3 起 Pak 与 IoStore 可共存）：
	 *   - Header.Type=Bin     -> 仅 BinBody 有效
	 *   - Header.Type=IoStore -> 仅 IoStoreBody 有效（独立 .utoc/.ucas 入口；当前未对外暴露）
	 *   - Header.Type=Pak     -> PakBody 必有；IoStoreBody 在 .pak 携带 .utoc/.ucas 同伴时一并存在
	 *                            （UE5 cook 产物的常态）。序列化时通过 bHasIoStoreCompanion 标志位
	 *                            判定是否一并写/读。
	 */
	TUniquePtr<FPPakPatchBody>     PakBody;
	TUniquePtr<FPBinPatchBody>     BinBody;
	TUniquePtr<FPIoStorePatchBody> IoStoreBody;

private:
	bool SetupWriter(const FString& InFilename);
	bool SetupReader(const FString& InFilename);
	bool RecordData(const uint8* InSource, const int64 InSize, FPPakPatchDataInfo& OutDataInfo);
	bool FinalizeWriteFile();

	/** 根据 Header.Type 序列化对应 Body（Serialize 过程 public+private 通用）。 */
	void SerializeBody(FArchive& Ar);

	/** 按 Header.Type 确保对应 Body UniquePtr 已实例化（Load 时用）。 */
	void EnsureBodyAllocated();

	FArchive* Reader = nullptr;
	FArchive* Writer = nullptr;
	FPPakPatcherMemory Data;
	EPPatchDataSourceType DataSourceType = EPPatchDataSourceType::None;
	bool bUsePrecache = true;
};

typedef TSharedPtr<FPResPatchData, ESPMode::ThreadSafe> FPResPatchDataPtr;
