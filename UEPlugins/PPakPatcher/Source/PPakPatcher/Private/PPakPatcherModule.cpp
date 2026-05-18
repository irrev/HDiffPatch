#include "PPakPatcherModule.h"
#include "PPatchManager.h"
#include "Utils/PPakPatcherUtils.h"
#include "HDiff/PHDiffPatcher.h"
#include "Patcher/PPakPatcher.h"

class PPAKPATCHER_API FPPakPatcherModule : public IPPakPatcherModule
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	virtual IPBinPatcher* GetBinPatcher() override;
	virtual TSharedPtr<FPPakPatcher> CreatePakPatcher() override;

private:
	/** 单实例 BinPatcher：模块生命周期内常驻；调用方借用裸指针。 */
	TUniquePtr<IPBinPatcher> BinPatcher;
};

void FPPakPatcherModule::StartupModule()
{
	BinPatcher = MakeUnique<FPHDiffPatcher>();
}

void FPPakPatcherModule::ShutdownModule()
{
	BinPatcher.Reset();
	FPPatchManager::TearDown();
}

IPBinPatcher* FPPakPatcherModule::GetBinPatcher()
{
	return BinPatcher.Get();
}

TSharedPtr<FPPakPatcher> FPPakPatcherModule::CreatePakPatcher()
{
	return MakeShared<FPPakPatcher>();
}

IMPLEMENT_MODULE(FPPakPatcherModule, PPakPatcher)
