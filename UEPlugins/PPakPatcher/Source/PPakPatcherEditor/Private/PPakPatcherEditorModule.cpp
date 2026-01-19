
#include "PPakPatcherEditorModule.h"
#include "CoreMinimal.h"
#include "Slate.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "PPakPatcherEditor"

/** IModuleInterface implementation */
void FPPakPatcherEditorModule::StartupModule()
{
}
void FPPakPatcherEditorModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPPakPatcherEditorModule, PPakPatcherEditor)