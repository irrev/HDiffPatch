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

	/**
	 * Pak patch 总体模式（决定是否解析 pak 结构 + PakAware 下的 entry 预处理层级）：
	 *   Binary                       : 整文件 HDiff（任意二进制资产）
	 *   PakAwareNoDecrypt            : 解析 pak，不解密，对密文+压缩字节做 diff
	 *   PakAwareDecryptAndCompress   : 解析 pak，解密后保持压缩态做 diff（**默认 / 唯一生产推荐**）
	 *   PakAwareDecryptAndDecompress : 解析 pak，解密+解压后做 diff（已禁用，入口拦截）
	 * 该值持久化到 patch Header；运行时按相同模式反向重组。
	 */
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	EPPakPatchMode PakPatchMode = EPPakPatchMode::PakAwareDecryptAndCompress;

	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	FString NewSignExtension = TEXT(".newsig");

	/** Load 时是否预先把整个 DataBlock 解压到内存（false = 流式按需解压，省内存）。 */
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bPrecachePatchDataOnLoad = false;

	/** Save 时是否先把整个 DataBlock 缓存在内存再一次性写盘。 */
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bPrecachePatchDataOnSave = false;

	/** PatchPak 时对 OldPak entry 做二次校验。 */
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bDoubleCheckEntry = true;

	// 以下 4 项控制 pak 元数据各 block 是否走二进制 patch（true）或全量记录（false）。
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bBinaryPatchIndexBlock = true;

	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bBinaryPatchPathHashBlock = true;

	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bBinaryPatchFullDirectoryBlock = true;

	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bBinaryPatchHeadBlock = true;

	/** 当前运行时无法获取 RSA 私钥，无法直接重新签名 pak；保持 false。 */
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bUseSignWriter = false;

	/** 把原始 .sig 内容序列化进 patch（运行时输出 .newsig，由业务层切换）。 */
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bRecordSignToPatch = true;

	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bGenPakFileMD5 = false;

	/** 运行时校验文件 hash 的算法（None / Crc32 / MD5）。 */
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	EPPakCheckFileHashType CheckFileHashType = EPPakCheckFileHashType::Crc32;

	/**
	 * v6: 启用 per-CompressionBlock 粒度 diff/patch（仅 DAC + Modify 生效）。
	 *   - true：当 New/Old entry 的 CompressionBlocks 数一致时，按 block 单独 diff/patch。
	 *     PatchPak 单 entry 工作集 ≈ 1 block（默认 64KB），避免大 entry 全量 buffer。
	 *   - false（默认）：强制走整 entry HDiff（兼容/对比基线用途）。
	 *   - 块数不一致 / 无压缩 entry 自动退化到整 entry HDiff（无需关闭此开关）。
	 *
	 * 历史：
	 *   2026-06-04 v6/v7 时代发现 !bUsePrecache 流式模式下 CRC mismatch（crypto-small DAC
	 *     pakchunk1307_0.pak）→ 临时默认改为 false。
	 *   2026-06-06 v8 重构后曾推测 bug 已间接修复，把默认改回 true 但**未实测**。
	 *   2026-06-16 实测仍然 CRC mismatch（同一 pakchunk1307_0.pak Expect:0x72876F66 vs
	 *     Actual:0xC09AD7CD），v8 没修复此 bug。Create/Apply EntryCountModifyPerBlock=86
	 *     已对齐（Apply 端 ++ 计数已修），但 patch 数据本身错了。
	 *   → 默认改回 false，调试和最终修复挂在 TODO #N5。
	 */
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bUsePerBlockDiff = false;

	/**
	 * 外部 per-entry 压缩（v8 起）：与 HDiff 内部 CompressType 无关。
	 * 每条 RecordData（entry diff / block patch / utoc/ucas diff 等）独立用 Oodle 压缩，
	 * 运行时按需流式解压。
	 *
	 * 默认禁用（None），原因：
	 *   - 启用后构建/运行侧 CPU 都会增加（每条记录都做一次压缩/解压）
	 *   - per-entry 压缩字典不跨 entry，整体压缩率比 v7 块压缩略差
	 *   - 收益需结合实际场景再评估
	 * 优势（与 v7 块压缩相比）：
	 *   - 构建侧无需 precache 整 DataBlock（流式落盘自然支持）
	 *   - 运行侧每实例额外内存 ≈ 单 entry 压缩字节数（不再是固定块缓存）
	 */
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	EPPatchExternalCompressType ExternalCompressType = EPPatchExternalCompressType::None;

	/** Oodle 压缩级别：-4..-1 HyperFast；0 None；1..4 SuperFast..Normal；5..9 Optimal1..5。 */
	UPROPERTY(config, EditAnywhere, Category = PakPatcher, meta = (ClampMin = "-4", ClampMax = "9"))
	int32 ExternalCompressLevel = 8;

	//--------- HDiff Settings ---------
	// 注意：以下 HDiff 参数（除特别说明外）仅 CreateDiff 时使用；Apply 端从 patch 元数据读取。
	// 因此 Apply 端命令行透传是无害冗余（commandlet 会 warn 提示）。

	/** 启用 single-compressed-diff 模式。生产固定 true。 */
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bUseSingleCompressMode = true;

	/**
	 * HDiff 匹配粒度阈值（仅 Create）。二进制 0..4 / 文本 4..9，HDiff 上游默认 6。
	 *
	 * 本项目默认改为 0（最激进匹配），依据：
	 *   crypto-full DAC（6665 modify entry / 16 GB）扫描 [0,3,6,9] 严格单调递增：
	 *     score=0: 2285.87 MB <-- best
	 *     score=3: 2290.12 MB
	 *     score=6: 2297.71 MB（HDiff 默认）
	 *     score=9: 2304.70 MB
	 *   score=0 vs 6: patch 体积 -11.84 MB / -0.515%；CreatePatch wall +2.7%（约 +6s）。
	 *   pak entry 经过 DAC（解密+压缩态）已是高熵 binary 数据，bin 范围（0..4）的下限 0
	 *   即可；再低无意义（HDiff 注释 bin: 0..4 / text: 4..9）。
	 *   详见 Test/Script/minscore_binsearch.py + hdiff_scan_results/minscore_results.json。
	 */
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	int32 MinSingleMatchScore = 0;

	/** 大缓存匹配（仅 Create）。DAC 模式（高熵）几乎无收益，保持 false。 */
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	bool bUseBigCacheMatch = false;

	/**
	 * HDiff 内部并行线程数（仅 Create）。与 PatchTaskThreadNum 嵌套时建议 1。
	 * 实测：N=1 → 2.8s；N=16 → 55s（慢 20x）。详见 HDiff参数扫描报告.md。
	 */
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	int32 ThreadNum = 1;

	/** HDiff patch 步长（仅 Create；Apply 从元数据读取，不读此字段）。默认 256KB。 */
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	int32 PatchStepMemSize = 262144;

	//--------- 任务级并发 ---------------------
	/**
	 * PPatchManager 处理每个 chunk 时的并发线程数：
	 *   < 1 : 主线程同步串行（零额外开销）
	 *   = 1 : 单异步线程串行（不阻塞 caller）
	 *   > 1 : 工作线程池并行处理多个 chunk
	 * 默认值（cpp 构造函数）：编辑器 = 16；移动端 = 1。与 ThreadNum 是不同维度。
	 */
	UPROPERTY(config, EditAnywhere, Category = PakPatcher)
	int32 PatchTaskThreadNum = 0;
};
