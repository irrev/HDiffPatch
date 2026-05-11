#include "PPakPatcherSettings.h"
#include "Misc/LazySingleton.h"
#include "Misc/ConfigCacheIni.h"

UPPakPatcherSettings::UPPakPatcherSettings(const FObjectInitializer& ObjInit)
{
}

UPPakPatcherSettings& UPPakPatcherSettings::Get()
{
	return *GetMutableDefault<UPPakPatcherSettings>();
}