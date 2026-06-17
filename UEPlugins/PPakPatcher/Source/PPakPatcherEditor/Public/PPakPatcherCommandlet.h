#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "Data/PPakPatcherDataType.h"
#include "PPakPatcherCommandlet.generated.h"

/*
* =====================================================================================================
*  PPakPatcher Commandlet 用法
*  通用调用形式：UnrealEditor-Cmd.exe MHAGame.uproject -run="PPakPatcher" -<Action> [-参数=值 ...]
* =====================================================================================================
*
*  通用可选参数（所有 Action 均可追加；未指定时使用 DefaultPPakPatcher.ini 的配置默认值；
*  以下所有 -Xxx=... 命令行覆盖只在当前 commandlet 进程内生效，不写回 ini）
*  -----------------------------------------------------------------------------------------------------
*    -Mode=Binary|                    生产/校验模式（合并了原 -Mode + -PakAwarePreprocess）；
*       PakAwareNoDecrypt|              缺省 PakAwareDecryptAndCompress（仅对 -CreatePakPatch /
*       PakAwareDecryptAndCompress|     -CreatePakPatchWithDir 生效；Check/Patch 端从 patch 头部自动读出，
*       PakAwareDecryptAndDecompress    无需指定）
*                                     旧别名（兼容）：PakAware → PakAwareDecryptAndCompress；
*                                       NoDecrypt / DecryptAndCompress / DecryptAndDecompress 也可单独传。
*                                     该值持久化到 patch Header，运行时 PatchPak 自动按相同 Mode 反向重组。
*    -Compress=None|ZLIB|LZMA|LZMA2|  HDiff 输出补丁的压缩算法；缺省 None
*              ZSTD|LDEF|BZ2          （仅 -CreatePakPatch / -CreatePakPatchWithDir 在 CreateDiff 时使用）
*    -CheckFileHashType=None|Crc32|   运行时校验旧/新文件哈希的算法；构建侧始终同时写 MD5+CRC32 到 Header，
*                       MD5            运行时按本参数选择性校验。缺省 Crc32。
*    -NoSingleCompress                关闭 HDiff 的 SingleCompressed 模式（等价 bUseSingleCompressMode=false）；
*                                     缺省开启。注意：生成端与校验/打补丁端必须保持一致，否则 Check/Patch 会失败
*    -ThreadNum=<n>                   HDiff 多线程数；缺省 1
*    -StepMemSize=<bytes>             HDiff 流式 patch 步长（运行时打补丁的内存上限）；缺省 262144 (256K)
*    -MinMatchScore=<n>               HDiff 单文件匹配评分；bin: 0~4, text: 4~9；缺省 0
*                                     （HDiff 上游 default=6；本项目按 crypto-full 实测结果改为 0：
*                                       score=0 vs 6 patch -0.515% / Create wall +2.7%）
*    -bUseBigCacheMatch               HDiff 大缓存匹配（O(oldSize) 内存换匹配速度）；缺省关闭
*    -bDoubleCheckEntry               PatchPak 时对 OldPak entry 做二次校验（默认开启）；
*    -NoDoubleCheckEntry              关闭二次校验
*
*  KeyChain 参数（仅当 pak 加密 / 签名时需要）
*  -----------------------------------------------------------------------------------------------------
*    Commandlet 入口会显式调用 FPPakPatcherKeyChainHelper::Get().GetKeyChain(true) 触发加载，
*    并通过 KeyChainUtilities::ApplyEncryptionKeys() 注入到引擎全局（FCoreDelegates::GetRegister...Delegate），
*    以确保后续 FPakFile/FPakInfo 解析加密 pak 时能取得到 key。加载结果会在日志里输出
*    "KeyChain loaded. EncryptionKeys=N, HasSigningKey=true/false"。
*
*    -cryptokeys="<Crypto.json 路径>"  推荐方式，包含 AES 加密 keys 与 RSA 签名 keys。
*                                     支持相对路径（基于项目根 ProjectDir 解析）和绝对路径。
*                                     等价于引擎 UnrealPak 的 -cryptokeys 参数。
*    -aes="<32 字节 ANSI 字符串>"      legacy AES key（仅 1 个 key，无法多 key 切换）
*    -EncryptionKeyOverrideGuid=<guid> 多 key 时切换 PrincipalEncryptionKey
*    -SignKey="<RSA-key.json>"        独立 RSA 签名 key（与 -cryptokeys 解耦）。仅在 -CreatePakPatch 生成
*                                     签名 pak 补丁时使用。JSON 格式：
*                                       { "PublicKey":  { "Exponent":"<b64>", "Modulus":"<b64>" },
*                                         "PrivateKey": { "Exponent":"<b64>", "Modulus":"<b64>" } }
*                                     若 -cryptokeys 也提供了 SigningKey，两处皆可，载入顺序为
*                                     -SignKey > -cryptokeys；后者会覆盖前者（同一 KeyChain 字段）。
*
* -----------------------------------------------------------------------------------------------------
*  Action: -CreatePakPatch
*  -----------------------------------------------------------------------------------------------------
*    功能：生成 New 与 Old 之间的补丁，支持单文件或目录
*    参数：
*       -NewPak="<新文件或目录>"     必填
*       -OldPak="<旧文件或目录>"     必填
*       -Patch="<patch 输出>"         必填（单文件时为目标文件路径，目录模式时为目标目录）
*    例：
*       UnrealEditor-Cmd.exe MHAGame.uproject -run="PPakPatcher" -CreatePakPatch
*           -NewPak="D:/New/chunk0.pak" -OldPak="D:/Old/chunk0.pak" -Patch="D:/Patch/chunk0.patch"
*           -Compress=ZSTD -ThreadNum=4
*       UnrealEditor-Cmd.exe MHAGame.uproject -run="PPakPatcher" -CreatePakPatch
*           -NewPak="D:/Bin/foo.bin" -OldPak="D:/Bin/foo_old.bin" -Patch="D:/Patch/foo.patch"
*           -Mode=Binary
*
* -----------------------------------------------------------------------------------------------------
*  Action: -CheckPakPatch
*  -----------------------------------------------------------------------------------------------------
*    功能：构建机回测 2 —— 调用 HDiff Check 接口校验 patch 数据合法
*    参数：
*       -NewPak="..."  -OldPak="..."  -Patch="..."     全部必填
*    例：
*       UnrealEditor-Cmd.exe MHAGame.uproject -run="PPakPatcher" -CheckPakPatch
*           -NewPak="..." -OldPak="..." -Patch="..."
*
* -----------------------------------------------------------------------------------------------------
*  Action: -PatchPak
*  -----------------------------------------------------------------------------------------------------
*    功能：用 Old + Patch 还原 New（也是构建机回测 1 的"打补丁"步骤）
*    参数：
*       -NewPak="<新文件输出>"  -OldPak="..."  -Patch="..."  全部必填
*
* -----------------------------------------------------------------------------------------------------
*  Action: -CreatePakPatchWithDir
*  -----------------------------------------------------------------------------------------------------
*    功能：根据 OldDir / NewDir 中的 md5_file_list.txt 一键产出补丁集（含 patch_manifest.txt）。
*          内部委托给 FPPatchManager::CreatePatch：
*            - 自动读 <NewDir>/md5_file_list.txt 与 <OldDir>/md5_file_list.txt
*            - 自动按 chunk 名匹配新旧 .pak（IoStore 同伴文件 .utoc/.ucas 由 FPPakPatcher 内部联动）
*            - 自动按 New/Old MD5 判定 DiffType（Equal/Modify/Add/Delete）
*            - Modify 项调 FPResPatcher::CreateDiff 产出 <NewBaseName>.patch
*            - Add    项把新文件直接拷入 PatchDir
*            - Delete 项仅记录到 manifest（不做物理删除）
*            - 所有条目落入 PatchDir/patch_manifest.txt
*    参数：
*       -NewDir="..."   必填，新版本资源根目录（含 md5_file_list.txt）
*       -OldDir="..."   必填，旧版本资源根目录（含 md5_file_list.txt）
*       -PatchDir="..." 必填，补丁输出根目录（不存在会自动创建）
*
* -----------------------------------------------------------------------------------------------------
*  Action: -PatchPakPatchWithDir
*  -----------------------------------------------------------------------------------------------------
*    功能：用 PatchDir 中的补丁把 ResDir 的本地资源升级到新版本。
*          内部流程：
*            1. VerifyBeforePatch ：校验本地 Old 资源 CRC 与 patch_manifest 期望一致（可选，默认开启）
*            2. ApplyPatch        ：按 patch_manifest 逐项处理 Modify/Add/Delete/Equal
*            3. VerifyAfterPatch  ：校验本地 New 资源 CRC（可选，默认开启）
*          每步独立计时并输出耗时。
*    参数：
*       -ResDir="..."   必填，本地资源根目录（含 md5_file_list.txt；会被就地修改）
*       -PatchDir="..." 必填，补丁根目录（含 patch_manifest.txt + .patch 文件）
*       -NoVerifyBefore 可选，跳过 VerifyBeforePatch
*       -NoVerifyAfter  可选，跳过 VerifyAfterPatch
*
* -----------------------------------------------------------------------------------------------------
*  Action: -SimpleTest / -UnitTest
*  -----------------------------------------------------------------------------------------------------
*    -SimpleTest：内置最小冒烟用例，无参数
*    -UnitTest ：端到端回测，参数：
*       -NewDir="..."  -OldDir="..."  -Output="..."   必填
*       -CheckMode=PatchAndCompare|HDiffCheck|Both    缺省 Both
*           PatchAndCompare：回测 1（产出 patch → patch old → 与 new 对比 MD5）
*           HDiffCheck     ：回测 2（额外调一次 FPPakPatcher::CheckPakDiff）
*           Both           ：先 PatchAndCompare 再 HDiffCheck
*/

UENUM()
enum class EPPakUnitTestCheckMode : uint8
{
	PatchAndCompare,	// 回测 1：patch old → 对比 new MD5
	HDiffCheck,			// 回测 2：HDiff CheckDiff 接口
	Both,				// 回测 1 + 回测 2
};

struct FPPakPatcherCommandletParams
{
	EPPakPatchMode PatchMode = EPPakPatchMode::PakAwareDecryptAndCompress;
	EPPakUnitTestCheckMode CheckMode = EPPakUnitTestCheckMode::Both;
	EPakPatchCompressType CompressType = EPakPatchCompressType::ZLIB;

	// -----------------------------------------------------------------------
	// 命令行覆盖 Settings 的字段（TOptional：未传 = 使用 ini 默认值；传了 = 覆盖）
	//   命令行覆盖只在当前 commandlet 进程内生效，不会写回 ini 文件。
	// -----------------------------------------------------------------------
	TOptional<EPPakCheckFileHashType>   CheckFileHashTypeOverride;
	TOptional<EPPatchExternalCompressType> ExternalCompressTypeOverride;  // -ExternalCompressType=Oodle_Mermaid
	TOptional<int32>  ExternalCompressLevelOverride;   // -ExternalCompressLevel=
	TOptional<bool>   UseSingleCompressModeOverride;   // -NoSingleCompress  → false
	TOptional<bool>   UseBigCacheMatchOverride;        // -bUseBigCacheMatch → true
	TOptional<bool>   DoubleCheckEntryOverride;        // -bDoubleCheckEntry / -NoDoubleCheckEntry
	TOptional<int32>  ThreadNumOverride;               // -ThreadNum=
	TOptional<int32>  StepMemSizeOverride;             // -StepMemSize=
	TOptional<int32>  MinMatchScoreOverride;           // -MinMatchScore=
	TOptional<int32>  PatchTaskThreadNumOverride;      // -PatchTaskThreadNum=

	void Parse(const FString& Params);
	void Print();

	/** 把所有 Override 应用到 UPPakPatcherSettings 单例（仅当 IsSet 时写入；HDiff 配置同时刷到 BinPatcher）。 */
	void ApplyOverridesToSettings() const;

	/**
	 * 修复 #26：在 Apply 端（PatchPak / PatchPakPatchWithDir / CheckPakPatch）action 入口
	 * 检查并 warn：用户传入的某些 HDiff 参数仅在 Create 端有效（StepMemSize 例外，
	 * Apply 端会从 patch 元数据读取，命令行透传是无害冗余）。
	 *   - ThreadNum / MinMatchScore / bUseBigCacheMatch / NoSingleCompress
	 *   - 这些参数仅 CreateDiff 时被 HDiff 算法使用；Apply 端透传到 Settings 仅作为冗余字段
	 *     不影响行为，但容易让用户误以为可以在 Apply 端改进性能。
	 * 检测到时打 Warning（不阻断），并给出明确解释。
	 */
	void WarnIrrelevantHDiffOverridesForApply(const TCHAR* InActionName) const;
};

UCLASS()
class UPPakPatcherCommandlet : public UCommandlet
{
	GENERATED_BODY()


public:
	/** Runs the commandlet */
	virtual int32 Main(const FString& Params) override;

private:
	FPPakPatcherCommandletParams Input;

	bool CheckFileParams(const TCHAR* Params, const TCHAR* Match, FString& OutParamValue, bool bCheckExist = false);
	bool CheckDirParams(const TCHAR* Params, const TCHAR* Match, FString& OutParamValue, bool bCheckExist = false, bool bCreateIfNotExist = false);

	int32 CreatePakPatch(const FString& Params);
	/** 单次产出补丁的核心函数；模式与压缩算法由成员 Input 提供（已在 Main() 中解析）。 */
	bool CreatePakPatch_Internal(const FString& InNewPakFilename, const FString& InOldPakFilename, const FString& InPatchFilename);
	int32 CheckPakPatch(const FString& Params);
	int32 PatchPak(const FString& Params);

	TArray<FString> GatherPaksInDirectory(const FString InDir);
	TMap<FString, FString> MakeNewOldMatchMap(const FString& InNewDir, const FString& InOldDir);
	int32 CreatePakPatchWithDir(const FString& Params);

	int32 PatchPakPatchWithDir(const FString& Params);

	int32 SimpleTest(const FString& Params);
	int32 UnitTest(const FString& Params);
};