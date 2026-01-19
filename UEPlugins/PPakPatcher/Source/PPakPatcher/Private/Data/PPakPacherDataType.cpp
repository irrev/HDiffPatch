#include "Data/PPakPacherDataType.h"

DEFINE_LOG_CATEGORY(LogPPakPacher);

#define PAKPATCHER_DEFINE_ENUM_TO_STRING(EnumType)\
FString FPPakPatcherEnumHelper::ToString(EnumType InEnumValue)\
{\
	const UEnum* EnumPtr = FindObject<UEnum>(ANY_PACKAGE, TEXT(#EnumType), true);\
	if (!EnumPtr) return FString("Invalid");\
\
	return EnumPtr->GetNameStringByIndex((int32)InEnumValue);\
}
PAKPATCHER_DEFINE_ENUM_TO_STRING(EPPatchDataSourceType)
PAKPATCHER_DEFINE_ENUM_TO_STRING(EPakFilePatchType)
#undef PAKPATCHER_DEFINE_ENUM_TO_STRING

UPPakPacherDataType::UPPakPacherDataType(const FObjectInitializer& ObjInit)
{
}
