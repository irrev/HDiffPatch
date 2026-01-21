#pragma once
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class PPAKPATCHEREDITOR_API FPPakPatcherEditorModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};