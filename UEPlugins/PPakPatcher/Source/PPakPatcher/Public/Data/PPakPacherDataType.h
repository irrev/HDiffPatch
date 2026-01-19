#pragma once
#include "CoreMinimal.h"
#include "PPakPacherDataType.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPPakPacher, Log, All);

enum class EPPatchDataSourceType : uint8
{
	None,
	Load,
	Record,
};

enum class EPakFilePatchType : uint8
{
	Keep,
	Modify,
	New,
	Delete,
};

#define PAKPATCHER_DECLEAR_ENUM_TO_STRING(EnumType) static FString ToString(EnumType InEnumValue)
class PPAKPATCHER_API FPPakPatcherEnumHelper
{
public:
	PAKPATCHER_DECLEAR_ENUM_TO_STRING(EPPatchDataSourceType);
	PAKPATCHER_DECLEAR_ENUM_TO_STRING(EPakFilePatchType);
};
#undef PAKPATCHER_DECLEAR_ENUM_TO_STRING

UCLASS()
class UPPakPacherDataType : public UObject
{
	GENERATED_UCLASS_BODY()
};

