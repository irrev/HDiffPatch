#include "Utils/PPakPatcherPerfReport.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogPPakPerfReport, Display, All);

TSharedPtr<FJsonObject> FPPakPatcherPerfReport::ToJson() const
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

	// Data stats
	TSharedPtr<FJsonObject> Stats = MakeShared<FJsonObject>();
	Stats->SetNumberField(TEXT("TotalOldAssetSize"), TotalOldAssetSize);
	Stats->SetNumberField(TEXT("TotalNewAssetSize"), TotalNewAssetSize);
	Stats->SetNumberField(TEXT("ModifyAssetPhysicalSize"), ModifyAssetPhysicalSize);
	Stats->SetNumberField(TEXT("IoStoreModifyPhysicalSize"), IoStoreModifyPhysicalSize);
	Stats->SetNumberField(TEXT("KeepAssetPhysicalSize"), KeepAssetPhysicalSize);
	Stats->SetNumberField(TEXT("NewAssetPhysicalSize"), NewAssetPhysicalSize);
	Stats->SetNumberField(TEXT("PakEntryDiffSize"), PakEntryDiffSize);
	Stats->SetNumberField(TEXT("IoStoreDiffSize"), IoStoreDiffSize);
	Stats->SetNumberField(TEXT("NewEntryFullDataSize"), NewEntryFullDataSize);
	Stats->SetNumberField(TEXT("EntryCountTotal"), EntryCountTotal);
	Stats->SetNumberField(TEXT("EntryCountKeep"), EntryCountKeep);
	Stats->SetNumberField(TEXT("EntryCountModify"), EntryCountModify);
	Stats->SetNumberField(TEXT("EntryCountNew"), EntryCountNew);
	Stats->SetNumberField(TEXT("EntryCountModifyPerBlock"), EntryCountModifyPerBlock);
	Root->SetObjectField(TEXT("Stats"), Stats);

	// Timing
	TSharedPtr<FJsonObject> Timing = MakeShared<FJsonObject>();
	Timing->SetNumberField(TEXT("Read"), TimeRead);
	Timing->SetNumberField(TEXT("Decrypt"), TimeDecrypt);
	Timing->SetNumberField(TEXT("Decompress"), TimeDecompress);
	Timing->SetNumberField(TEXT("Diff"), TimeDiff);
	Timing->SetNumberField(TEXT("Patch"), TimePatch);
	Timing->SetNumberField(TEXT("Compress"), TimeCompress);
	Timing->SetNumberField(TEXT("Encrypt"), TimeEncrypt);
	Timing->SetNumberField(TEXT("Write"), TimeWrite);
	Timing->SetNumberField(TEXT("Total"), TimeTotal);
	Root->SetObjectField(TEXT("Timing"), Timing);

	return Root;
}

bool FPPakPatcherPerfReport::SaveToFile(const FString& FilePath) const
{
	TSharedPtr<FJsonObject> Json = ToJson();
	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	if (!FJsonSerializer::Serialize(Json.ToSharedRef(), Writer))
	{
		return false;
	}
	return FFileHelper::SaveStringToFile(Output, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void FPPakPatcherPerfReport::LogSummary(const TCHAR* Phase) const
{
	auto MB = [](int64 Bytes) { return (double)Bytes / (1024.0 * 1024.0); };

	UE_LOG(LogPPakPerfReport, Display, TEXT(""));
	UE_LOG(LogPPakPerfReport, Display, TEXT("========== PerfReport [%s] =========="), Phase);
	UE_LOG(LogPPakPerfReport, Display, TEXT("  Entries: Total=%d Keep=%d Modify=%d New=%d (PerBlock=%d)"),
		EntryCountTotal, EntryCountKeep, EntryCountModify, EntryCountNew, EntryCountModifyPerBlock);
	UE_LOG(LogPPakPerfReport, Display, TEXT("  Sizes:  ModifyPhysical=%.2fMB  IoStoreModifyPhysical=%.2fMB  PakEntryDiff=%.2fMB  IoStoreDiff=%.2fMB  NewFullData=%.2fMB"),
		MB(ModifyAssetPhysicalSize), MB(IoStoreModifyPhysicalSize), MB(PakEntryDiffSize), MB(IoStoreDiffSize), MB(NewEntryFullDataSize));
	const int64 TotalModifyPhysical = ModifyAssetPhysicalSize + IoStoreModifyPhysicalSize;
	if (TotalModifyPhysical > 0)
	{
		UE_LOG(LogPPakPerfReport, Display, TEXT("  Ratio:  (PakEntryDiff+IoStoreDiff)/TotalModifyPhysical = %.2f%%"),
			(double)(PakEntryDiffSize + IoStoreDiffSize) / (double)TotalModifyPhysical * 100.0);
	}
	UE_LOG(LogPPakPerfReport, Display, TEXT("  Timing: Total=%.2fs Read=%.2fs Decrypt=%.2fs Decompress=%.2fs"),
		TimeTotal, TimeRead, TimeDecrypt, TimeDecompress);
	UE_LOG(LogPPakPerfReport, Display, TEXT("          Diff=%.2fs Patch=%.2fs Compress=%.2fs Encrypt=%.2fs Write=%.2fs"),
		TimeDiff, TimePatch, TimeCompress, TimeEncrypt, TimeWrite);
	UE_LOG(LogPPakPerfReport, Display, TEXT("=========================================="));
}

void FPPakPatcherPerfReport::MergeFrom(const FPPakPatcherPerfReport& Other)
{
	TotalOldAssetSize        += Other.TotalOldAssetSize;
	TotalNewAssetSize        += Other.TotalNewAssetSize;
	ModifyAssetPhysicalSize  += Other.ModifyAssetPhysicalSize;
	IoStoreModifyPhysicalSize+= Other.IoStoreModifyPhysicalSize;
	KeepAssetPhysicalSize    += Other.KeepAssetPhysicalSize;
	NewAssetPhysicalSize     += Other.NewAssetPhysicalSize;
	PakEntryDiffSize         += Other.PakEntryDiffSize;
	IoStoreDiffSize          += Other.IoStoreDiffSize;
	NewEntryFullDataSize     += Other.NewEntryFullDataSize;
	EntryCountTotal          += Other.EntryCountTotal;
	EntryCountKeep           += Other.EntryCountKeep;
	EntryCountModify         += Other.EntryCountModify;
	EntryCountNew            += Other.EntryCountNew;
	EntryCountModifyPerBlock += Other.EntryCountModifyPerBlock;

	TimeRead       += Other.TimeRead;
	TimeDecrypt    += Other.TimeDecrypt;
	TimeDecompress += Other.TimeDecompress;
	TimeDiff       += Other.TimeDiff;
	TimePatch      += Other.TimePatch;
	TimeCompress   += Other.TimeCompress;
	TimeEncrypt    += Other.TimeEncrypt;
	TimeWrite      += Other.TimeWrite;
	TimeTotal      += Other.TimeTotal;
}

void FPPakPatcherPerfReport::ThreadSafeMergeFrom(const FPPakPatcherPerfReport& Other)
{
	FScopeLock Lock(&MergeCS);
	MergeFrom(Other);
}
