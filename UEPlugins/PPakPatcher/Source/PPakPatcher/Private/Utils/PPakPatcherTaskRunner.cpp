// Copyright (c) Tencent. All rights reserved.
#include "Utils/PPakPatcherTaskRunner.h"
#include "PPakPatcherModule.h"
#include "HAL/PlatformProcess.h"
#include "GenericPlatform/GenericPlatformProcess.h"

FPPakPatcherTaskRunner::FPPakPatcherTaskRunner(int32 InThreadNum, const TCHAR* InTagForLog)
	: Tag(InTagForLog)
{
	if (InThreadNum < 1)
	{
		EffectiveThreadNum = -1; // Sync
	}
	else
	{
		EffectiveThreadNum = InThreadNum; // 1=单异步串行（限流到 1），>1=多线程并行
		SlotFreeEvent = FPlatformProcess::GetSynchEventFromPool(false /*ManualReset=false 自动重置*/);
	}

	UE_LOG(LogPPakPacher, Display,
		TEXT("[%s] Init: InputThreadNum=%d Mode=%s EffectiveThreadNum=%d"),
		*Tag, InThreadNum, GetModeDesc(), EffectiveThreadNum);
}

FPPakPatcherTaskRunner::~FPPakPatcherTaskRunner()
{
	// 兜底等待：caller 没调 Wait 时也要确保 Futures 全部 Get（否则 ThreadPool 任务可能丢失结果），
	// 重置 bWaited 让兜底真的能跑到 Get
	if (Futures.Num() > 0)
	{
		bWaited.store(false);
		Wait();
	}
	if (SlotFreeEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(SlotFreeEvent);
		SlotFreeEvent = nullptr;
	}
}

const TCHAR* FPPakPatcherTaskRunner::GetModeDesc() const
{
	if (EffectiveThreadNum < 0) return TEXT("Sync");
	if (EffectiveThreadNum == 1) return TEXT("SingleAsync");
	return TEXT("Parallel");
}

void FPPakPatcherTaskRunner::WaitForSlot()
{
	// 简单的"等空槽"循环：当 RunningCount 达到上限时阻塞，直到某个任务完成触发 SlotFreeEvent
	while (true)
	{
		{
			FScopeLock Lock(&SlotCS);
			if (RunningCount < EffectiveThreadNum)
			{
				++RunningCount;
				return;
			}
		}
		// 满了 → 等事件
		if (SlotFreeEvent)
		{
			SlotFreeEvent->Wait();
		}
	}
}

void FPPakPatcherTaskRunner::Submit(FTaskFunc&& InFunc)
{
	const int32 TaskIdx = TaskCount++;

	if (EffectiveThreadNum < 0)
	{
		// Sync：直接在调用线程执行
		const bool bOk = InFunc(TaskIdx);
		// 用一个已完成的 future 占位，便于 Wait() 统一汇总
		TPromise<bool> Promise;
		Promise.SetValue(bOk);
		Futures.Add(Promise.GetFuture());
		return;
	}

	// EffectiveThreadNum >= 1：异步执行，由 WaitForSlot 限流
	//   = 1：单异步串行（同时只有 1 个 task 在跑，但与 caller 异步）
	//   > 1：多线程并行（最多 N 个 task 并发）
	WaitForSlot();
	TFuture<bool> F = Async(EAsyncExecution::ThreadPool, [this, Func = MoveTemp(InFunc), TaskIdx]() -> bool
	{
		bool bOk = false;
		{
			// 安全包一层：异常或 false 都要释放槽位
			struct FSlotGuard
			{
				FPPakPatcherTaskRunner* Self;
				~FSlotGuard()
				{
					{
						FScopeLock Lock(&Self->SlotCS);
						--Self->RunningCount;
					}
					if (Self->SlotFreeEvent)
					{
						Self->SlotFreeEvent->Trigger();
					}
				}
			} Guard{ this };

			bOk = Func(TaskIdx);
		}
		return bOk;
	});
	Futures.Add(MoveTemp(F));
}

bool FPPakPatcherTaskRunner::Wait()
{
	// 防御重复 Wait：第二次调用直接返回上次结果（实际上这里返回 true 因为 Futures 已 Reset 不会再走 Get()）
	bool bExpected = false;
	if (!bWaited.compare_exchange_strong(bExpected, true))
	{
		UE_LOG(LogPPakPacher, Warning,
			TEXT("[%s] Wait() called multiple times; ignoring subsequent calls."), *Tag);
		return true;
	}

	int32 NumOk = 0;
	int32 NumFail = 0;
	for (TFuture<bool>& F : Futures)
	{
		const bool bOk = F.Get();
		if (bOk) ++NumOk;
		else     ++NumFail;
	}
	Futures.Reset();

	UE_LOG(LogPPakPacher, Display,
		TEXT("[%s] Wait done. Total=%d Ok=%d Fail=%d Mode=%s"),
		*Tag, TaskCount, NumOk, NumFail, GetModeDesc());

	return NumFail == 0;
}
