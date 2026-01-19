#include "PPakPatcherModule.h"
#include "Utils/PPakPatcherUtils.h"
#include "HDiff/PHDiffPatcher.h"
#include "PPakPatcher.h"

class PPAKPATCHER_API FPPakPatcherModule : public IPPakPatcherModule
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual IPBinPatcher* GetBinPatcher() override;
	virtual IPPakPatcher* GetPakPatcher() override;
protected:
	TSharedPtr<IPBinPatcher> BinPatcherInstance;
	TSharedPtr<IPPakPatcher> PakPatcherInstance;
};

void FPPakPatcherModule::StartupModule()
{
	//const FString NewFileName = TEXT("G:\\TestHDiff\\TestHDiff\\TestData\\new.txt");
	//const FString OldFileName = TEXT("G:\\TestHDiff\\TestHDiff\\TestData\\old.txt");
	//FPPakPatcherUtils::TestBinaryPatch(NewFileName, OldFileName);

	//{
	//	const FString NewPakfileName = TEXT("G:\\dolphinapk\\0.0.27.1_Patch\\pakchunk4_4342ca6be03a8f4fc08dd602033bb24c_0_P.pak");
	//	const FString OldPakfileName = TEXT("G:\\dolphinapk\\0.0.26.1_Patch\\pakchunk4_1b4c1fd8a6d86283a75f78b16a365424_0_P.pak");
	//	FPPakPatcherUtils::TestPakPatch(NewPakfileName, OldPakfileName);
	//}

	//{
	//	const FString NewPakfileName = TEXT("G:\\dolphinapk\\0.0.27.1_Patch\\pakchunk2_a20c1868cf7617c254f0e7e4d77801cf_0_P.pak");
	//	const FString OldPakfileName = TEXT("G:\\dolphinapk\\0.0.26.1_Patch\\pakchunk2_1b775a48feebc0357a67f8f196bc419b_0_P.pak");
	//	FPPakPatcherUtils::TestPakPatch(NewPakfileName, OldPakfileName);
	//}


	//{
	//	const FString NewPakfileName = TEXT("H:\\testpak\\win02-uncomp\\pakchunk2.pak");
	//	const FString OldPakfileName = TEXT("H:\\testpak\\win01-uncomp\\pakchunk2.pak");
	//	FPPakPatcherUtils::TestPakPatch(NewPakfileName, OldPakfileName);
	//}

	//{
	//	const FString NewPakfileName = TEXT("H:\\testpak\\win02-comp\\pakchunk1.pak");
	//	const FString OldPakfileName = TEXT("H:\\testpak\\win01-comp\\pakchunk1.pak");
	//	FPPakPatcherUtils::TestPakPatch(NewPakfileName, OldPakfileName);
	//}

	int32 aaa=1;
}

void FPPakPatcherModule::ShutdownModule()
{

}

IPBinPatcher* FPPakPatcherModule::GetBinPatcher()
{
	if (!BinPatcherInstance.IsValid())
	{
		BinPatcherInstance = MakeShared<FPHDiffPatcher>();
	}
	return BinPatcherInstance.Get();
}

IPPakPatcher* FPPakPatcherModule::GetPakPatcher()
{
	if (!PakPatcherInstance.IsValid())
	{
		PakPatcherInstance = MakeShared<FPPakPatcher>();
	}
	return PakPatcherInstance.Get();
}


IMPLEMENT_MODULE(FPPakPatcherModule, PPakPatcher)