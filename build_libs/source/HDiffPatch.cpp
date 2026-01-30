// HDiffPatch.cpp : Defines the entry point for the application.
//

#include "HDiffPatch.h"

#if HDIFFPATCH_ENABLE_DIFF
#include "libHDiffPatch/HDiff/diff.h"
#include "../../compress_plugin_demo.h"
#include "../../decompress_plugin_demo.h"
#endif //HDIFFPATCH_ENABLE_DIFF
#include "libHDiffPatch/HPatch/patch.h"
#include <stdlib.h>
#include <string.h>

namespace HDiffPatch
{
#if HDIFFPATCH_ENABLE_DIFF
	static hdiff_TCompress* _getCompressPlugin(HDiffCompressionType compressType) {
		switch (compressType) {
#ifdef _CompressPlugin_zlib
		case HDIFF_COMPRESSION_ZLIB: return (hdiff_TCompress*)&zlibCompressPlugin;
#endif
#ifdef _CompressPlugin_lzma
		case HDIFF_COMPRESSION_LZMA: return (hdiff_TCompress*)&lzmaCompressPlugin;
#endif
#ifdef _CompressPlugin_lzma2
		case HDIFF_COMPRESSION_LZMA2: return (hdiff_TCompress*)&lzma2CompressPlugin;
#endif
#ifdef _CompressPlugin_zstd
		case HDIFF_COMPRESSION_ZSTD: return (hdiff_TCompress*)&zstdCompressPlugin;
#endif
#ifdef _CompressPlugin_ldef
		case HDIFF_COMPRESSION_LDEF: return (hdiff_TCompress*)&ldefCompressPlugin;
#endif
#ifdef _CompressPlugin_bz2
		case HDIFF_COMPRESSION_BZ2: return (hdiff_TCompress*)&bz2CompressPlugin;
#endif
		default: return 0;
		}
	}

	static hpatch_TDecompress* _getDecompressPlugin(HDiffCompressionType compressType) {
		switch (compressType) {
#ifdef _CompressPlugin_zlib
		case HDIFF_COMPRESSION_ZLIB: return &zlibDecompressPlugin;
#endif
#ifdef _CompressPlugin_lzma
		case HDIFF_COMPRESSION_LZMA: return &lzmaDecompressPlugin;
#endif
#ifdef _CompressPlugin_lzma2
		case HDIFF_COMPRESSION_LZMA2: return &lzma2DecompressPlugin;
#endif
#ifdef _CompressPlugin_zstd
		case HDIFF_COMPRESSION_ZSTD: return &zstdDecompressPlugin;
#endif
#ifdef _CompressPlugin_ldef
		case HDIFF_COMPRESSION_LDEF: return &ldefDecompressPlugin;
#endif
#ifdef _CompressPlugin_bz2
		case HDIFF_COMPRESSION_BZ2: return &bz2DecompressPlugin;
#endif
		default: return 0;
		}
	}

	static hpatch_TDecompress* _getDecompressPluginByStr(const char* type) {
		if (strlen(type) == 0) return 0;
#ifdef _CompressPlugin_zlib
		if ((strcmp(type, "zlib") == 0) || (strcmp(type, "pzlib") == 0)) return &zlibDecompressPlugin;
#endif
#ifdef _CompressPlugin_lzma
		if (strcmp(type, "lzma") == 0) return &lzmaDecompressPlugin;
#endif
#ifdef _CompressPlugin_lzma2
		if (strcmp(type, "lzma2") == 0) return &lzma2DecompressPlugin;
#endif
#ifdef _CompressPlugin_zstd
		if (strcmp(type, "zstd") == 0) return &zstdDecompressPlugin;
#endif
#ifdef _CompressPlugin_ldef
		if ((strcmp(type, "zlib") == 0) || (strcmp(type, "pzlib") == 0)) return &ldefDecompressPlugin;
#endif
#ifdef _CompressPlugin_bz2
		if ((strcmp(type, "bz2") == 0) || (strcmp(type, "bzip2") == 0) || (strcmp(type, "pbz2") == 0) || (strcmp(type, "pbzip2") == 0)) return &bz2DecompressPlugin;
#endif
		return 0;
	}

	void CreateDiff(const unsigned char* newData, const unsigned char* newData_end,
		const unsigned char* oldData, const unsigned char* oldData_end,
		std::vector<unsigned char>& out_diff,
		int kMinSingleMatchScore /*= _kMinSingleMatchScore_default*/, bool isUseBigCacheMatch /*= false*/, size_t threadNum /*= 1*/)
	{
		return create_diff(newData, newData_end, oldData, oldData_end, out_diff, kMinSingleMatchScore, isUseBigCacheMatch, threadNum);
	}

	void CreateSingleCompressedDiff(const unsigned char* newData, const unsigned char* newData_end,
		const unsigned char* oldData, const unsigned char* oldData_end,
		std::vector<unsigned char>& out_diff,
		HDiffCompressionType compressType /*= HDIFF_COMPRESSION_NONE*/,
		int kMinSingleMatchScore /*= _kMinSingleMatchScore_default*/,
		size_t patchStepMemSize /*= 1024 * 256*/,
		bool isUseBigCacheMatch /*= false*/, size_t threadNum /*= 1*/)
	{
		hdiff_TCompress* compressPlugin = _getCompressPlugin(compressType);
		if (compressPlugin && compressPlugin->setParallelThreadNumber) {
			compressPlugin->setParallelThreadNumber(compressPlugin, (int)threadNum);
		}
		create_single_compressed_diff(newData, newData_end, oldData, oldData_end, out_diff, compressPlugin, kMinSingleMatchScore, patchStepMemSize, isUseBigCacheMatch, 0, threadNum);
	}

	bool CheckSingleCompressedDiff(const unsigned char* newData, const unsigned char* newData_end,
		const unsigned char* oldData, const unsigned char* oldData_end,
		const unsigned char* diff, const unsigned char* diff_end,
		size_t threadNum /*= 1*/)
	{

		SingleCompressedDiffInfo info;
		if (!GetSingleCompressedDiffInfo(diff, diff_end, &info)) return false;

		hpatch_TDecompress* decompressPlugin = 0;
		if (strlen(info.compressType) > 0) {
			decompressPlugin = _getDecompressPluginByStr(info.compressType);
			if (!decompressPlugin) return false;
		}

		return check_single_compressed_diff(newData, newData_end, oldData, oldData_end, diff, diff_end, decompressPlugin, threadNum);
	}

	bool CheckDiff(const unsigned char* newData, const unsigned char* newData_end,
		const unsigned char* oldData, const unsigned char* oldData_end,
		const unsigned char* diff, const unsigned char* diff_end)
	{
		return check_diff(newData, newData_end, oldData, oldData_end, diff, diff_end);
	}
#endif //HDIFFPATCH_ENABLE_DIFF

	bool Patch(unsigned char* out_newData, unsigned char* out_newData_end,
		const unsigned char* oldData, const unsigned char* oldData_end,
		const unsigned char* serializedDiff, const unsigned char* serializedDiff_end)
	{
		return patch(out_newData, out_newData_end, oldData, oldData_end, serializedDiff, serializedDiff_end) == hpatch_TRUE;
	}

	struct TAutoMem {
		unsigned char* _mem;
		TAutoMem() :_mem(0) {}
		~TAutoMem() { if (_mem) free(_mem); }
		void realloc(size_t size) {
			if (_mem) free(_mem);
			_mem = (unsigned char*)malloc(size);
		}
	};

	bool GetSingleCompressedDiffInfo(const unsigned char* diff, const unsigned char* diff_end,
		SingleCompressedDiffInfo* out_info)
	{
		hpatch_singleCompressedDiffInfo info;
		if (!getSingleCompressedDiffInfo_mem(&info, diff, diff_end)) return false;
		out_info->newDataSize = info.newDataSize;
		out_info->oldDataSize = info.oldDataSize;
		out_info->uncompressedSize = info.uncompressedSize;
		out_info->compressedSize = info.compressedSize;
		out_info->diffDataPos = info.diffDataPos;
		out_info->coverCount = info.coverCount;
		out_info->stepMemSize = info.stepMemSize;
		memcpy(out_info->compressType, info.compressType, sizeof(out_info->compressType));
		return true;
	}

	bool PatchSingleCompressedDiff(unsigned char* out_newData, unsigned char* out_newData_end,
		const unsigned char* oldData, const unsigned char* oldData_end,
		const unsigned char* diff, const unsigned char* diff_end,
		const SingleCompressedDiffInfo* info,
		size_t threadNum)
	{
		hpatch_TDecompress* decompressPlugin = 0;
		if (strlen(info->compressType) > 0) {
			decompressPlugin = _getDecompressPluginByStr(info->compressType);
			if (!decompressPlugin) return false; // unknown compression type or plugin not enabled
		}

		hpatch_TStreamOutput out_newStream;
		hpatch_TStreamInput  oldStream;
		hpatch_TStreamInput  diffStream;
		mem_as_hStreamOutput(&out_newStream, out_newData, out_newData_end);
		mem_as_hStreamInput(&oldStream, oldData, oldData_end);
		mem_as_hStreamInput(&diffStream, diff, diff_end);

		size_t cacheSize = (size_t)(info->stepMemSize + hpatch_kStreamCacheSize * 3);
		TAutoMem mem;
		mem.realloc(cacheSize);
		unsigned char* temp_cache = mem._mem;
		unsigned char* temp_cache_end = temp_cache + cacheSize;

		return patch_single_compressed_diff(&out_newStream, &oldStream, &diffStream,
			info->diffDataPos, info->uncompressedSize, info->compressedSize,
			decompressPlugin, info->coverCount, (size_t)info->stepMemSize, temp_cache, temp_cache_end, 0, threadNum) == hpatch_TRUE;
	}

	static hpatch_BOOL _onDiffInfo(sspatch_listener_t* listener,
		const hpatch_singleCompressedDiffInfo* info,
		hpatch_TDecompress** out_decompressPlugin,
		unsigned char** out_temp_cache,
		unsigned char** out_temp_cacheEnd) {
		if (strlen(info->compressType) > 0) {
			*out_decompressPlugin = _getDecompressPluginByStr(info->compressType);
			if (!*out_decompressPlugin) return hpatch_FALSE; // unknown compression type or plugin not enabled
		}
		else {
			*out_decompressPlugin = 0;
		}

		size_t cacheSize = (size_t)(info->stepMemSize + hpatch_kStreamCacheSize * 3);
		TAutoMem* mem = (TAutoMem*)listener->import;
		mem->realloc(cacheSize);
		*out_temp_cache = mem->_mem;
		*out_temp_cacheEnd = mem->_mem + cacheSize;
		return hpatch_TRUE;
	}

	bool PatchSingleStream(unsigned char* out_newData, unsigned char* out_newData_end,
		const unsigned char* oldData, const unsigned char* oldData_end,
		const unsigned char* diff, const unsigned char* diff_end,
		size_t threadNum)
	{
		TAutoMem mem;
		sspatch_listener_t listener = { 0 };
		listener.import = &mem;
		listener.onDiffInfo = _onDiffInfo;

		return patch_single_stream_mem(&listener, out_newData, out_newData_end,
			oldData, oldData_end, diff, diff_end, 0, threadNum) == hpatch_TRUE;
	}
};