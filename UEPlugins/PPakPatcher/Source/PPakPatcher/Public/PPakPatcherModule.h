#pragma once
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "IPPakPatcher.h"
#include "IPBinPatcher.h"


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
	 * Third party binary patcher. Support patching between two memories.
	 */
	virtual IPBinPatcher* GetBinPatcher() = 0;

	/**
	 * Pak patcher. Support patching between two pak files.
	 */
	virtual IPPakPatcher* GetPakPatcher() = 0;
};