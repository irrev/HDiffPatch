// HDiffPatch.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <iostream>

// TODO: Reference additional headers your program requires here.



//#pragma once

#ifdef __cplusplus
#define EXTERN extern "C"
#else
#define EXTERN
#endif

#ifdef HDIFFPATCH_STATIC_LIB
	// for build lib
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
#else
	// for test app
	#define HDIFFPATCH_API EXTERN
#endif

//#if defined(HDIFFPATCH_PLATFORM_WINDOWS)||defined(HDIFFPATCH_PLATFORM_LINUX)||defined(HDIFFPATCH_PLATFORM_MACOS)
//#define HDIFFPATCH_ENABLE_DIFF 1
//#else
//#define HDIFFPATCH_ENABLE_DIFF 0
//#endif

#define HDIFFPATCH_ENABLE_DIFF 1

#include <vector>

namespace HDiffPatch
{
	static const int _kMinSingleMatchScore_default = 6;

	enum HDiffCompressionType {
		HDIFF_COMPRESSION_NONE = 0,
		HDIFF_COMPRESSION_ZLIB,
		HDIFF_COMPRESSION_LZMA,
		HDIFF_COMPRESSION_LZMA2,
		HDIFF_COMPRESSION_ZSTD,
		HDIFF_COMPRESSION_LDEF,
		HDIFF_COMPRESSION_BZ2
	};

	struct SingleCompressedDiffInfo {
		unsigned long long newDataSize;      // size of new data (file)
		unsigned long long oldDataSize;      // size of old data (file)
		unsigned long long uncompressedSize; // size of uncompressed diff data (excluding header)
		unsigned long long compressedSize;   // size of compressed diff data (excluding header), 0 means not compressed
		unsigned long long diffDataPos;      // offset of diff data in the stream (after header)
		unsigned long long coverCount;       // number of cover (copy/match) blocks
		unsigned long long stepMemSize;      // recommended memory size for single-step patch processing
		char               compressType[260];// compression algorithm type name (e.g. "zlib", "lzma"), empty means not compressed
	};

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

	//create a single compressed diff data between oldData and newData
	//  out_diff is compressed(if compressPlugin not null), you can use patch_single_stream() or patch_single_compressed_diff()
	//  kMinSingleMatchScore: default 6, bin: 0--4  text: 4--9
	//  patchStepMemSize: default 256k, recommended 64k,2m etc...
	//  isUseBigCacheMatch: big cache max used O(oldSize) memory, match speed faster, but build big cache slow 
	HDIFFPATCH_API void CreateSingleCompressedDiff(const unsigned char* newData, const unsigned char* newData_end,
		const unsigned char* oldData, const unsigned char* oldData_end,
		std::vector<unsigned char>& out_diff,
		HDiffCompressionType compressType = HDIFF_COMPRESSION_NONE,
		int kMinSingleMatchScore = _kMinSingleMatchScore_default,
		size_t patchStepMemSize = 1024 * 256,
		bool isUseBigCacheMatch = false, size_t threadNum = 1);

	//return patch_single_?(oldData+diff)==newData?
	HDIFFPATCH_API bool CheckSingleCompressedDiff(const unsigned char* newData, const unsigned char* newData_end,
		const unsigned char* oldData, const unsigned char* oldData_end,
		const unsigned char* diff, const unsigned char* diff_end,
		HDiffCompressionType compressType = HDIFF_COMPRESSION_NONE,
		size_t threadNum = 1);
#endif //HDIFFPATCH_ENABLE_DIFF

	//get single compressed diff info
	HDIFFPATCH_API bool GetSingleCompressedDiffInfo(const unsigned char* diff, const unsigned char* diff_end,
		SingleCompressedDiffInfo* out_info);

	//patch by single compressed diff with info
	HDIFFPATCH_API bool PatchSingleCompressedDiff(unsigned char* out_newData, unsigned char* out_newData_end,
		const unsigned char* oldData, const unsigned char* oldData_end,
		const unsigned char* diff, const unsigned char* diff_end,
		const SingleCompressedDiffInfo* info,
		size_t threadNum = 1);

	//generate newData by patch(oldData + serializedDiff)
	//  serializedDiff create by create_diff()
	HDIFFPATCH_API bool Patch(unsigned char* out_newData, unsigned char* out_newData_end,
		const unsigned char* oldData, const unsigned char* oldData_end,
		const unsigned char* serializedDiff, const unsigned char* serializedDiff_end);

	//patch by single compressed diff
	//  diff create by create_single_compressed_diff()
	HDIFFPATCH_API bool PatchSingleStream(unsigned char* out_newData, unsigned char* out_newData_end,
		const unsigned char* oldData, const unsigned char* oldData_end,
		const unsigned char* diff, const unsigned char* diff_end,
		size_t threadNum = 1);

	// ================================================================
	// Easy API wrappers
	// ================================================================

	// Create a patch (compressed diff)
	// This is a wrapper for CreateSingleCompressedDiff
	HDIFFPATCH_API void CreatePatch(const unsigned char* oldData, const unsigned char* oldData_end,
		const unsigned char* newData, const unsigned char* newData_end,
		std::vector<unsigned char>& out_patch,
		HDiffCompressionType compressType = HDIFF_COMPRESSION_ZSTD,
		int kMinSingleMatchScore = _kMinSingleMatchScore_default,
		size_t threadNum = 1);

	// Apply a patch
	// This is a wrapper that automatically handles GetSingleCompressedDiffInfo and memory allocation
	HDIFFPATCH_API bool ApplyPatch(const unsigned char* oldData, const unsigned char* oldData_end,
		const unsigned char* patch, const unsigned char* patch_end,
		std::vector<unsigned char>& out_newData,
		size_t threadNum = 1);

	// Check a patch
	// This is a wrapper that automatically detects compression type
	HDIFFPATCH_API bool CheckPatch(const unsigned char* oldData, const unsigned char* oldData_end,
		const unsigned char* newData, const unsigned char* newData_end,
		const unsigned char* patch, const unsigned char* patch_end,
		size_t threadNum = 1);

};