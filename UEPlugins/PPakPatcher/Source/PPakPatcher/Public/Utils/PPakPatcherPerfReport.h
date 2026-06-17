#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "Dom/JsonObject.h"

/**
 * 性能报告：记录 CreatePakDiff / PatchPak 各阶段的耗时和数据量。
 * 构建/运行侧各维护一份，最终序列化为 JSON 输出到 PatchDir。
 */
struct PPAKPATCHER_API FPPakPatcherPerfReport
{
	// --- 数据量统计 ---
	int64 TotalOldAssetSize = 0;
	int64 TotalNewAssetSize = 0;
	int64 ModifyAssetPhysicalSize = 0;
	int64 IoStoreModifyPhysicalSize = 0;
	int64 KeepAssetPhysicalSize = 0;
	int64 NewAssetPhysicalSize = 0;
	int64 PakEntryDiffSize = 0;
	int64 IoStoreDiffSize = 0;
	int64 NewEntryFullDataSize = 0;
	int32 EntryCountTotal = 0;
	int32 EntryCountKeep = 0;
	int32 EntryCountModify = 0;
	int32 EntryCountNew = 0;
	// v6: per-CompressionBlock 路径命中计数（仅 DAC + 压缩 entry + 块数一致 时启用）
	int32 EntryCountModifyPerBlock = 0;

	// --- 各阶段累计耗时（秒）---
	double TimeRead = 0.0;
	double TimeDecrypt = 0.0;
	double TimeDecompress = 0.0;
	double TimeDiff = 0.0;
	double TimePatch = 0.0;
	double TimeCompress = 0.0;
	double TimeEncrypt = 0.0;
	double TimeWrite = 0.0;
	double TimeTotal = 0.0;

	// --- 计时起点 slot（每个阶段独立，避免互相覆盖）---
	// 用法见下方 PPATCHER_PERF_BEGIN/END 宏
	double T0_TimeRead = 0.0;
	double T0_TimeDecrypt = 0.0;
	double T0_TimeDecompress = 0.0;
	double T0_TimeDiff = 0.0;
	double T0_TimePatch = 0.0;
	double T0_TimeCompress = 0.0;
	double T0_TimeEncrypt = 0.0;
	double T0_TimeWrite = 0.0;

	// --- 输出 ---
	TSharedPtr<FJsonObject> ToJson() const;
	bool SaveToFile(const FString& FilePath) const;
	void LogSummary(const TCHAR* Phase) const;

	// --- 合并 ---
	void MergeFrom(const FPPakPatcherPerfReport& Other);
	void ThreadSafeMergeFrom(const FPPakPatcherPerfReport& Other);

	// --- 计时辅助（手工调用）---
	static double Now() { return FPlatformTime::Seconds(); }
	static double Since(double T0) { return FPlatformTime::Seconds() - T0; }

private:
	FCriticalSection MergeCS;
};

/**
 * 计时宏：把起点 T0 存到 PerfReport 内部对应字段的 slot，避免外部声明临时变量。
 *
 * 用法（ReportPtr 可能为 nullptr，此时整对宏零开销）：
 *
 *   PPATCHER_PERF_BEGIN(&PerfReport, TimeRead);
 *   // ... 执行读操作 ...
 *   PPATCHER_PERF_END(&PerfReport, TimeRead);
 *
 * 注意事项：
 *   1) BEGIN 和 END 必须在同一线程、同一 PerfReport 实例上配对调用
 *   2) 同一个 Field 不可嵌套（如 BEGIN(Read) ... BEGIN(Read) ... END ... END 会算错）
 *   3) 不同 Field 可以同时存在（slot 独立），如 BEGIN(Read) BEGIN(Decrypt) ... END(Decrypt) END(Read)
 */
#define PPATCHER_PERF_BEGIN(ReportPtr, Field) \
	do { if ((ReportPtr) != nullptr) { (ReportPtr)->T0_##Field = FPPakPatcherPerfReport::Now(); } } while (0)

#define PPATCHER_PERF_END(ReportPtr, Field) \
	do { if ((ReportPtr) != nullptr) { (ReportPtr)->Field += FPPakPatcherPerfReport::Since((ReportPtr)->T0_##Field); } } while (0)
