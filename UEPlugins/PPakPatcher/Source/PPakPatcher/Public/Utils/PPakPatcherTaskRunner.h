// Copyright (c) Tencent. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "Async/Async.h"
#include "Async/Future.h"
#include "HAL/Event.h"
#include <atomic>

/**
 * FPPakPatcherTaskRunner
 *
 * 任务级并发运行器，供 PPatchManager 在循环处理 chunk 时使用。
 *
 * 使用方式：
 *   FPPakPatcherTaskRunner Runner(ThreadNum);
 *   for (...) {
 *       Runner.Submit([=] (int32 TaskIdx) {
 *           // 这里调用 FPResPatcher::CreateDiff / PatchDiff
 *           // 注意：每个 task lambda 必须只访问自己的局部状态（独立 FPResPatcher 实例）
 *       });
 *   }
 *   bool bAllOk = Runner.Wait();
 *
 * 三种执行模式（由构造时的 InThreadNum 决定）：
 *   - InThreadNum < 1 : 同步执行（主线程立刻执行 lambda）
 *   - InThreadNum = 1 : 单一异步线程（任务串行排队，不阻塞 caller）
 *   - InThreadNum > 1 : 多线程并行（最多 N 个任务并发执行）
 *
 * 线程安全约定：
 *   - **本类非 thread-safe：仅支持单线程顺序调用 Submit + Wait（生命周期内 Wait 只能调一次）**
 *   - 析构兜底会再 Wait 一次（如果之前没调 Wait），但析构期间不能并发 Submit
 *   - lambda 内部不可共享外部可写状态（除非有锁保护）
 *   - lambda 必须按值捕获所需变量（避免栈失效）
 *   - lambda 内部对外部计数器 ++ 等操作必须用原子或锁
 *   - **lambda 不能 throw / check 失败**：会让 ThreadPool 工作线程崩溃，TaskRunner 状态错乱；
 *      用 bool 返回 + UE_LOG(Error) 表达失败
 */
class PPAKPATCHER_API FPPakPatcherTaskRunner
{
public:
	using FTaskFunc = TFunction<bool(int32 /*TaskIdx*/)>;

	/**
	 * @param InThreadNum 见类注释
	 * @param InTagForLog 日志前缀
	 */
	explicit FPPakPatcherTaskRunner(int32 InThreadNum, const TCHAR* InTagForLog = TEXT("TaskRunner"));
	~FPPakPatcherTaskRunner();

	/**
	 * 提交一个任务。任务会按构造时的模式调度执行。
	 * 任务返回 false 视为失败（Wait 会汇总所有失败，返回 false）。
	 */
	void Submit(FTaskFunc&& InFunc);

	/**
	 * 阻塞当前线程直到所有提交的任务完成。
	 * @return 全部任务都成功返回 true；任一失败返回 false。
	 */
	bool Wait();

	/** 当前已提交任务数（含已完成）。 */
	int32 GetTaskCount() const { return TaskCount; }

	/** 返回实际生效的并发数（用于日志）。 */
	int32 GetEffectiveThreadNum() const { return EffectiveThreadNum; }

	/** 返回模式描述字符串（用于日志）。 */
	const TCHAR* GetModeDesc() const;

private:
	/** 多线程模式下：等待至少一个槽位空闲（即 RunningCount < EffectiveThreadNum） */
	void WaitForSlot();

	int32 EffectiveThreadNum = 0; // -1=Sync, 1=SingleAsync, >=2=Parallel
	FString Tag;

	int32 TaskCount = 0;
	TArray<TFuture<bool>> Futures;

	/** 防御性 guard：标记 Wait 已被调用过（重复调用是无操作）。 */
	std::atomic<bool> bWaited{ false };

	// 多线程模式下，限制并发数：用一个事件 + 计数器控制
	FCriticalSection SlotCS;
	int32 RunningCount = 0;
	FEvent* SlotFreeEvent = nullptr; // ManualReset，AutoReset 都行；这里用 AutoReset
};
