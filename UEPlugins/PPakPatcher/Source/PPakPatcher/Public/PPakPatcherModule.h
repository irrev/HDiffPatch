#pragma once
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Data/PPakPatcherDataType.h"
#include "Patcher/IPBinPatcher.h"

class FPPakPatcher;


class PPAKPATCHER_API IPPakPatcherModule : public IModuleInterface
{
public:
	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IPPakPatcherModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IPPakPatcherModule>("PPakPatcher");
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("PPakPatcher");
	}

	/**
	 * Third party binary patcher（单实例，模块持有；调用方借用裸指针，不延长生命周期）。
	 * BinPatcher 内部为无状态服务（所有数据通过参数传入/传出），故全模块共享一个实例足以。
	 * 多线程并发调用 CreateDiff/Patch/CheckDiff 是安全的（无可变成员状态）。
	 */
	virtual IPBinPatcher* GetBinPatcher() = 0;

	/**
	 * Pak patcher（每次创建新实例）。
	 * 内部持有 pak 缓存（CachedPakData）等 per-task 状态，调用方各自持有 TSharedPtr，用完释放。
	 * IoStore (.utoc/.ucas) 会在 FPPakPatcher 内部按需调用 FPIoStorePatcher 处理。
	 */
	virtual TSharedPtr<FPPakPatcher> CreatePakPatcher() = 0;
};
