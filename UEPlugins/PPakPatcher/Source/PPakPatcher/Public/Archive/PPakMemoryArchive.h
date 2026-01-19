#pragma once

#include "CoreMinimal.h"
#include "CoreUObject.h"
#include "CoreTypes.h"
#include "UObject/NameTypes.h"
#include "Engine/EngineTypes.h"
#include "Serialization/MemoryArchive.h"
#include "Archive/PPakPatcherMemory.h"


class PPAKPATCHER_API FPPakMemoryArchive : public FMemoryArchive
{
public:
	FPPakMemoryArchive(const int64 PreAllocateBytes = 0, bool bIsPersistent = false, const TCHAR* InFilename = nullptr);

	virtual void Serialize(void* InData, int64 Num) override;

	/**
	* Returns the name of the Archive.  Useful for getting the name of the package a struct or object
	* is in when a loading error occurs.
	*
	* This is overridden for the specific Archive Types
	**/
	virtual FString GetArchiveName() const override;

	/**
	 * Gets the total size of the data written
	 */
	virtual int64 TotalSize() override
	{
		return Data.GetSize();
	}

	int64 GetSize() const
	{
		return Data.GetSize();
	}

	/**
	 * Returns the written data. To release this archive's ownership of the data, call ReleaseOwnership()
	 */
	uint8* GetData() const;

	void Resize(int64 InNewSize);

	void Reserve(int64 NewMax);
	/**
	 * Releases ownership of the written data
	 *
	 * Also returns the pointer, so that the caller only needs to call this function to take control
	 * of the memory.
	 */
	FORCEINLINE uint8* ReleaseOwnership()
	{
		return Data.ReleaseOwnership();
	}

	bool operator==(const FPPakMemoryArchive& Other);
	bool operator!=(const FPPakMemoryArchive& Other);

private:

	FPPakPatcherMemory Data;

	/** Non-copyable */
	FPPakMemoryArchive(const FPPakMemoryArchive&) = delete;
	FPPakMemoryArchive& operator=(const FPPakMemoryArchive&) = delete;


	/** Archive name, used for debugging, by default set to NAME_None. */
	const FString ArchiveName;
};