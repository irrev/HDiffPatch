#include "PHDiffPatcher.h"

#if USE_HDIFFPATCH
#include <HDiffPatch.h>
#endif

FPHDiffPatcher::FPHDiffPatcher()
{

}

FPHDiffPatcher::~FPHDiffPatcher()
{

}

bool FPHDiffPatcher::CreateDiff(const TArray<uint8>& InNew, const TArray<uint8>& InOld, TArray<uint8>& OutDiff)
{
#if USE_HDIFFPATCH
	std::vector<unsigned char> StdTmp;
	HDiffPatch::CreateDiff(
		InNew.GetData(), InNew.GetData() + InNew.Num(),
		InOld.GetData(), InOld.GetData() + InOld.Num(),
		StdTmp,
		MinSingleMatchScore, bUseBigCacheMatch, ThreadNum);
	OutDiff.SetNumUninitialized(StdTmp.size());
	FMemory::Memcpy(OutDiff.GetData(), StdTmp.data(), StdTmp.size());
	return true;
#endif
	return false;
}

bool FPHDiffPatcher::CreateDiff(uint8* InNew, uint64 InNewSize, uint8* InOld, uint64 InOldSize, TArray<uint8>& OutDiff)
{
#if USE_HDIFFPATCH
	std::vector<unsigned char> StdTmp;
	HDiffPatch::CreateDiff(
		InNew, InNew + InNewSize,
		InOld, InOld + InOldSize,
		StdTmp,
		MinSingleMatchScore, bUseBigCacheMatch, ThreadNum);
	OutDiff.SetNumUninitialized(StdTmp.size());
	FMemory::Memcpy(OutDiff.GetData(), StdTmp.data(), StdTmp.size());
	return true;
#endif
	return false;
}

bool FPHDiffPatcher::CheckDiff(const TArray<uint8>& InNew, const TArray<uint8>& InOld, const TArray<uint8>& InDiff)
{
#if USE_HDIFFPATCH
	return HDiffPatch::CheckDiff(
		InNew.GetData(), InNew.GetData() + InNew.Num(),
		InOld.GetData(), InOld.GetData() + InOld.Num(),
		InDiff.GetData(), InDiff.GetData() + InDiff.Num());
#endif
	return false;
}

bool FPHDiffPatcher::CheckDiff(uint8* InNew, uint64 InNewSize, uint8* InOld, uint64 InOldSize, uint8* InDiff, uint64 InDiffSize)
{
#if USE_HDIFFPATCH
	return HDiffPatch::CheckDiff(
		InNew, InNew + InNewSize,
		InOld, InOld + InOldSize,
		InDiff, InDiff + InDiffSize);
#endif
	return false;
}

bool FPHDiffPatcher::Patch(TArray<uint8>& OutNew, const TArray<uint8>& InOld, const TArray<uint8>& InDiff)
{
#if USE_HDIFFPATCH
	return HDiffPatch::Patch(OutNew.GetData(), OutNew.GetData() + OutNew.Num(),
		InOld.GetData(), InOld.GetData() + InOld.Num(),
		InDiff.GetData(), InDiff.GetData() + InDiff.Num());
#endif
	return false;
}

bool FPHDiffPatcher::Patch(uint8* InNew, uint64 InNewSize, uint8* InOld, uint64 InOldSize, uint8* InDiff, uint64 InDiffSize)
{
#if USE_HDIFFPATCH
	return HDiffPatch::Patch(InNew, InNew + InNewSize,
		InOld, InOld + InOldSize,
		InDiff, InDiff + InDiffSize);
#endif
	return false;
}
