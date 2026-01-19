#pragma once

#include "CoreMinimal.h"
#include "Misc/KeyChainUtilities.h"


class PPAKPATCHER_API FPPakPatcherKeyChainHelper
{
public:
	FPPakPatcherKeyChainHelper(){}
	~FPPakPatcherKeyChainHelper(){}

	static FPPakPatcherKeyChainHelper& Get();

	FKeyChain& GetKeyChain(bool bForceReload = false);
	bool Signed();
	bool HasEncryptionKey();

private:
	void LoadKeyChain(bool bForceReload);
	void LoadKeyChainInEditor();
	void LoadKeyChainInGame();

	bool LoadKeyChainFromFile();
	bool LoadKeyChainFromEngineIni();
	bool LoadKeyChainFromCommandline();

	bool bLoadOnce = true;
	FKeyChain KeyChain;
};
