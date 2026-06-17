#include "PPakPatcherSettings.h"
#include "Misc/LazySingleton.h"
#include "Misc/ConfigCacheIni.h"

UPPakPatcherSettings::UPPakPatcherSettings(const FObjectInitializer& ObjInit)
{
	// 任务级并发默认值：编辑器（构建侧）16 线程并行处理 chunk；移动端（运行侧）单异步线程不阻塞主线程。
	// ini 配置可覆盖；后续根据实测调整。
#if WITH_EDITOR
	PatchTaskThreadNum = 16;
#else
	PatchTaskThreadNum = 1;
#endif
}

UPPakPatcherSettings& UPPakPatcherSettings::Get()
{
	return *GetMutableDefault<UPPakPatcherSettings>();
}