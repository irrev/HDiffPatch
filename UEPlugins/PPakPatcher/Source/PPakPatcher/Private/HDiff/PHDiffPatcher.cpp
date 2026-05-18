#include "PHDiffPatcher.h"
#include "PPakPatcherSettings.h"

#if USE_HDIFFPATCH
#include <HDiffPatch.h>
#endif

FPHDiffPatcher::FPHDiffPatcher()
{
	// 不在构造函数中调 ApplySettingsFromConfig()——
	// Module::StartupModule 阶段 CDO (GetDefault<UPPakPatcherSettings>) 可能尚未初始化，
	// 调用会导致 EXCEPTION_ACCESS_VIOLATION 崩溃。
	// Settings 会在以下时机被加载：
	//   1. Commandlet Main 调 Input.ApplyOverridesToSettings() 后显式调 ReloadSettingsFromConfig()
	//   2. 首次调用 CreateDiff/Patch 等 API 时通过 EnsureSettingsLoaded() 懒加载
	bSettingsLoaded = false;
}

FPHDiffPatcher::~FPHDiffPatcher()
{

}


void FPHDiffPatcher::ApplySettingsFromConfig()
{
	if(const UPPakPatcherSettings* Config = GetDefault<UPPakPatcherSettings>())
	{
		bUseSingleCompressMode = Config->bUseSingleCompressMode;
		MinSingleMatchScore = Config->MinSingleMatchScore;
		PatchStepMemSize = Config->PatchStepMemSize;
		bUseBigCacheMatch = Config->bUseBigCacheMatch;
		ThreadNum = Config->ThreadNum;
		bSettingsLoaded = true;
	}
}

bool FPHDiffPatcher::CreateDiff(const TArray<uint8>& InNew, const TArray<uint8>& InOld, TArray<uint8>& OutDiff,
	EPakPatchCompressType InCompressType/* = EPakPatchCompressType::None*/)
{
	EnsureSettingsLoaded();
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
	EnsureSettingsLoaded();
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
