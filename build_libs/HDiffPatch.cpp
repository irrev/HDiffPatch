// HDiffPatch.cpp : Defines the entry point for the application.
//

#include "HDiffPatch.h"

#if HDIFFPATCH_EXPORT_DIFF
#include "..\libHDiffPatch\HDiff\diff.h"
#endif //HDIFFPATCH_EXPORT_DIFF
#include "..\libHDiffPatch\HPatch\patch.h"

namespace HDiffPatch
{
#if HDIFFPATCH_EXPORT_DIFF
	void CreateDiff(const unsigned char* newData, const unsigned char* newData_end,
		const unsigned char* oldData, const unsigned char* oldData_end,
		std::vector<unsigned char>& out_diff,
		int kMinSingleMatchScore /*= _kMinSingleMatchScore_default*/, bool isUseBigCacheMatch /*= false*/, size_t threadNum /*= 1*/)
	{
		return create_diff(newData, newData_end, oldData, oldData_end, out_diff, kMinSingleMatchScore, isUseBigCacheMatch, threadNum);
	}

	bool CheckDiff(const unsigned char* newData, const unsigned char* newData_end,
		const unsigned char* oldData, const unsigned char* oldData_end,
		const unsigned char* diff, const unsigned char* diff_end)
	{
		return check_diff(newData, newData_end, oldData, oldData_end, diff, diff_end);
	}
#endif //HDIFFPATCH_EXPORT_DIFF

	bool Patch(unsigned char* out_newData, unsigned char* out_newData_end,
		const unsigned char* oldData, const unsigned char* oldData_end,
		const unsigned char* serializedDiff, const unsigned char* serializedDiff_end)
	{
		return patch(out_newData, out_newData_end, oldData, oldData_end, serializedDiff, serializedDiff_end) == hpatch_TRUE;
	}
};