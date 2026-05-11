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

bool FPHDiffPatcher::CreateDiff(const TArray<uint8>& InNew, const TArray<uint8>& InOld, TArray<uint8>& OutDiff,
	EPakPatchCompressType InCompressType/* = EPakPatchCompressType::None*/)
{
#if USE_HDIFFPATCH
	std::vector<unsigned char> StdTmp;
	if (bUseSingleCompressMode)
	{
		HDiffPatch::CreateSingleCompressedDiff(
			InNew.GetData(), InNew.GetData() + InNew.Num(),
			InOld.GetData(), InOld.GetData() + InOld.Num(),
			StdTmp,
			(HDiffPatch::HDiffCompressionType)InCompressType,
			MinSingleMatchScore,
			PatchStepMemSize,
			bUseBigCacheMatch,
			ThreadNum
		);
		OutDiff.SetNumUninitialized(StdTmp.size());
		FMemory::Memcpy(OutDiff.GetData(), StdTmp.data(), StdTmp.size());
	}
	else
	{
		HDiffPatch::CreateDiff(
			InNew.GetData(), InNew.GetData() + InNew.Num(),
			InOld.GetData(), InOld.GetData() + InOld.Num(),
			StdTmp);
		OutDiff.SetNumUninitialized(StdTmp.size());
		FMemory::Memcpy(OutDiff.GetData(), StdTmp.data(), StdTmp.size());
	}
	
	return true;
#else
	return false;
#endif
}

bool FPHDiffPatcher::CreateDiff(uint8* InNew, uint64 InNewSize, uint8* InOld, uint64 InOldSize, TArray<uint8>& OutDiff,
	EPakPatchCompressType InCompressType /* = EPakPatchCompressType::None*/)
{
#if USE_HDIFFPATCH
	std::vector<unsigned char> StdTmp;

	if (bUseSingleCompressMode)
	{
		HDiffPatch::CreateSingleCompressedDiff(
			InNew, InNew + InNewSize,
			InOld, InOld + InOldSize,
			StdTmp,
			(HDiffPatch::HDiffCompressionType)InCompressType,
			MinSingleMatchScore,
			PatchStepMemSize,
			bUseBigCacheMatch,
			ThreadNum);
		OutDiff.SetNumUninitialized(StdTmp.size());
		FMemory::Memcpy(OutDiff.GetData(), StdTmp.data(), StdTmp.size());
	}
	else
	{
		HDiffPatch::CreateDiff(
			InNew, InNew + InNewSize,
			InOld, InOld + InOldSize,
			StdTmp,
			MinSingleMatchScore, bUseBigCacheMatch, ThreadNum);
		OutDiff.SetNumUninitialized(StdTmp.size());
		FMemory::Memcpy(OutDiff.GetData(), StdTmp.data(), StdTmp.size());
	}
	return true;
#else
	return false;
#endif
}

bool FPHDiffPatcher::CheckDiff(const TArray<uint8>& InNew, const TArray<uint8>& InOld, const TArray<uint8>& InDiff)
{
#if USE_HDIFFPATCH
	if (bUseSingleCompressMode)
	{
		return HDiffPatch::CheckSingleCompressedDiff(
			InNew.GetData(), InNew.GetData() + InNew.Num(),
			InOld.GetData(), InOld.GetData() + InOld.Num(),
			InDiff.GetData(), InDiff.GetData() + InDiff.Num(),
			ThreadNum);
	}
	else
	{
		return HDiffPatch::CheckDiff(
			InNew.GetData(), InNew.GetData() + InNew.Num(),
			InOld.GetData(), InOld.GetData() + InOld.Num(),
			InDiff.GetData(), InDiff.GetData() + InDiff.Num());
	}
#else
	return false;
#endif
}

bool FPHDiffPatcher::CheckDiff(uint8* InNew, uint64 InNewSize, uint8* InOld, uint64 InOldSize, uint8* InDiff, uint64 InDiffSize)
{
#if USE_HDIFFPATCH
	if (bUseSingleCompressMode)
	{
		return HDiffPatch::CheckSingleCompressedDiff(
			InNew, InNew + InNewSize,
			InOld, InOld + InOldSize,
			InDiff, InDiff + InDiffSize,
			ThreadNum);
	}
	else
	{
		return HDiffPatch::CheckDiff(
			InNew, InNew + InNewSize,
			InOld, InOld + InOldSize,
			InDiff, InDiff + InDiffSize);
	}
#else
	return false;
#endif
}

bool FPHDiffPatcher::Patch(TArray<uint8>& OutNew, const TArray<uint8>& InOld, const TArray<uint8>& InDiff)
{
#if USE_HDIFFPATCH
	if (bUseSingleCompressMode)
	{
		HDiffPatch::SingleCompressedDiffInfo Info;
		HDiffPatch::GetSingleCompressedDiffInfo(InDiff.GetData(), InDiff.GetData() + InDiff.Num(), &Info);
		
		return HDiffPatch::PatchSingleCompressedDiff(
			OutNew.GetData(), OutNew.GetData() + OutNew.Num(),
			InOld.GetData(), InOld.GetData() + InOld.Num(),
			InDiff.GetData(), InDiff.GetData() + InDiff.Num(),
			&Info,
			ThreadNum
		);
	}
	else
	{
		return HDiffPatch::Patch(
			OutNew.GetData(), OutNew.GetData() + OutNew.Num(),
			InOld.GetData(), InOld.GetData() + InOld.Num(),
			InDiff.GetData(), InDiff.GetData() + InDiff.Num()
		);
	}
#else
	return false;
#endif
}

bool FPHDiffPatcher::Patch(uint8* InNew, uint64 InNewSize, uint8* InOld, uint64 InOldSize, uint8* InDiff, uint64 InDiffSize)
{
#if USE_HDIFFPATCH
	if (bUseSingleCompressMode)
	{
		HDiffPatch::SingleCompressedDiffInfo Info;
		HDiffPatch::GetSingleCompressedDiffInfo(InDiff, InDiff + InDiffSize, &Info);

		return HDiffPatch::PatchSingleCompressedDiff(
			InNew, InNew + InNewSize,
			InOld, InOld + InOldSize,
			InDiff, InDiff + InDiffSize,
			&Info,
			ThreadNum
		);
	}
	else
	{
		return HDiffPatch::Patch(
			InNew, InNew + InNewSize,
			InOld, InOld + InOldSize,
			InDiff, InDiff + InDiffSize
		);
	}
#else
	return false;
#endif
}
