// HDiffPatch.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <iostream>
#include <vector>

// TODO: Reference additional headers your program requires here.



//#pragma once

#ifdef __cplusplus
#define EXTERN extern "C"
#else
#define EXTERN
#endif

#if HDIFFPATCH_STATIC_LIB
	#if defined(HDIFFPATCH_EXPORTS)
		#define HDIFFPATCH_API EXTERN
	#else
		#define HDIFFPATCH_API EXTERN
	#endif
#else
	#ifdef HDIFFPATCH_PLATFORM_WINDOWS
	#define HDIFFPATCH_IMPORT __declspec(dllimport)
	#define HDIFFPATCH_EXPORT __declspec(dllexport)
	#endif

	#ifdef HDIFFPATCH_PLATFORM_ANDROID
	#define HDIFFPATCH_EXPORT __attribute__((visibility("default")))
	#define HDIFFPATCH_IMPORT __attribute__((visibility("default")))
	#endif

	#ifdef HDIFFPATCH_PLATFORM_MACOS
	#define HDIFFPATCH_EXPORT __attribute__((visibility("default")))
	#define HDIFFPATCH_IMPORT __attribute__((visibility("default")))
	#endif

	#ifdef HDIFFPATCH_PLATFORM_IOS
	#define HDIFFPATCH_EXPORT __attribute__((visibility("default")))
	#define HDIFFPATCH_IMPORT
	#endif

	#ifdef HDIFFPATCH_PLATFORM_LINUX
	#define HDIFFPATCH_EXPORT __attribute__((visibility("default")))
	#define HDIFFPATCH_IMPORT __attribute__((visibility("default")))
	#endif

	#if defined(HDIFFPATCH_EXPORTS)
		#define HDIFFPATCH_API EXTERN HDIFFPATCH_EXPORT
	#else
		#define HDIFFPATCH_API EXTERN HDIFFPATCH_IMPORT
	#endif
#endif //HDIFFPATCH_STATIC_LIB

//#if defined(HDIFFPATCH_PLATFORM_WINDOWS)||defined(HDIFFPATCH_PLATFORM_LINUX)||defined(HDIFFPATCH_PLATFORM_MACOS)
//#define HDIFFPATCH_ENABLE_DIFF 1
//#else
//#define HDIFFPATCH_ENABLE_DIFF 0
//#endif

#define HDIFFPATCH_ENABLE_DIFF 1

namespace HDiffPatch
{
	static const int _kMinSingleMatchScore_default = 6;

#if HDIFFPATCH_ENABLE_DIFF
	//create a diff data between oldData and newData
	//  out_diff is uncompressed, you can use create_compressed_diff()
	//       or create_single_compressed_diff() create compressed diff data
	//  recommended always use create_single_compressed_diff() replace create_diff()
	//  kMinSingleMatchScore: default 6, bin: 0--4  text: 4--9
	//  isUseBigCacheMatch: big cache max used O(oldSize) memory, match speed faster, but build big cache slow 
	HDIFFPATCH_API void CreateDiff(const unsigned char* newData, const unsigned char* newData_end,
		const unsigned char* oldData, const unsigned char* oldData_end,
		std::vector<unsigned char>& out_diff,
		int kMinSingleMatchScore = _kMinSingleMatchScore_default,
		bool isUseBigCacheMatch = false, size_t threadNum = 1);

	//return patch(oldData+diff)==newData?
	HDIFFPATCH_API bool CheckDiff(const unsigned char* newData, const unsigned char* newData_end,
		const unsigned char* oldData, const unsigned char* oldData_end,
		const unsigned char* diff, const unsigned char* diff_end);
#endif //HDIFFPATCH_ENABLE_DIFF

	//generate newData by patch(oldData + serializedDiff)
	//  serializedDiff create by create_diff()
	HDIFFPATCH_API bool Patch(unsigned char* out_newData, unsigned char* out_newData_end,
		const unsigned char* oldData, const unsigned char* oldData_end,
		const unsigned char* serializedDiff, const unsigned char* serializedDiff_end);
};