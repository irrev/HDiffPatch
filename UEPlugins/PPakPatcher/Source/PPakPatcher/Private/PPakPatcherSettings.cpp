#include "PPakPatcherSettings.h"
#include "Misc/LazySingleton.h"



FPPakPatcherSettings& FPPakPatcherSettings::Get()
{
	FPPakPatcherSettings& Singleton = TLazySingleton<FPPakPatcherSettings>::Get();
	if (!Singleton.bHasLoaded)
	{
		Singleton.Load();
	}
	return Singleton;
}

void FPPakPatcherSettings::Load()
{
	static const TCHAR* Section = TEXT("PPakPatcher");
	FConfigFile EngineSettings;
	FConfigCacheIni::LoadLocalIniFile(EngineSettings, TEXT("Engine"), true, nullptr);

	EngineSettings.GetString(Section, TEXT("NewSignExtension"), NewSignExtension);

	EngineSettings.GetBool(Section, TEXT("bPrecachePatchDataOnLoad"), bPrecachePatchDataOnLoad);
	EngineSettings.GetBool(Section, TEXT("bPrecachePatchDataOnSave"), bPrecachePatchDataOnSave);
	EngineSettings.GetBool(Section, TEXT("bDoubleCheckEntry"), bDoubleCheckEntry);

	EngineSettings.GetBool(Section, TEXT("bBinaryPatchIndexBlock"), bBinaryPatchIndexBlock);
	EngineSettings.GetBool(Section, TEXT("bBinaryPatchPathBlock"), bBinaryPatchPathBlock);
	EngineSettings.GetBool(Section, TEXT("bBinaryPatchHeadBlock"), bBinaryPatchHeadBlock);


	EngineSettings.GetBool(Section, TEXT("bUseSignWriter"), bUseSignWriter);
	EngineSettings.GetBool(Section, TEXT("bRecordSignToPatch"), bRecordSignToPatch);

	bHasLoaded = true;
}
