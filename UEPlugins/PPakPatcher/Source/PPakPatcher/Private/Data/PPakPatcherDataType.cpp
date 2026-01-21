#include "Data/PPakPatcherDataType.h"

DEFINE_LOG_CATEGORY(LogPPakPacher);


#if ENGINE_MAJOR_VERSION >= 5
#define PAKPATCHER_DEFINE_ENUM_TO_STRING(EnumType)\
FString FPPakPatcherEnumHelper::ToString(EnumType InEnumValue)\
{\
	return UEnum::GetValueAsString(InEnumValue);\
}
#else
#define PAKPATCHER_DEFINE_ENUM_TO_STRING(EnumType)\
FString FPPakPatcherEnumHelper::ToString(EnumType InEnumValue)\
{\
	const UEnum* EnumPtr = FindObject<UEnum>(ANY_PACKAGE, TEXT(#EnumType), true);\
	if (!EnumPtr) return FString("Invalid");\
\
	return EnumPtr->GetNameStringByIndex((int32)InEnumValue);\
}
#endif
PAKPATCHER_DEFINE_ENUM_TO_STRING(EPPatchDataSourceType)
PAKPATCHER_DEFINE_ENUM_TO_STRING(EPakFilePatchType)
#undef PAKPATCHER_DEFINE_ENUM_TO_STRING

UPPakPatcherDataType::UPPakPatcherDataType(const FObjectInitializer& ObjInit)
{
}
