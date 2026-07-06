#include "AssetPullExecutor.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"

#define LOCTEXT_NAMESPACE "AssetPuller"

namespace
{
	/** True if the package already exists in the target under EITHER extension (.uasset/.umap). */
	bool TargetPackageFileExists(const FString& TargetFile)
	{
		if (IFileManager::Get().FileExists(*TargetFile))
		{
			return true;
		}
		const bool bIsMapPath = TargetFile.EndsWith(TEXT(".umap"));
		const FString Sibling = FPaths::GetBaseFilename(TargetFile, /*bRemovePath*/false)
			+ (bIsMapPath ? TEXT(".uasset") : TEXT(".umap"));
		return IFileManager::Get().FileExists(*Sibling);
	}
}

FPullReport FAssetPullExecutor::Execute(const FPullPlan& Plan, bool bShowProgressDialog)
{
	FPullReport Report;

	TArray<const FPullItem*> ToCopy;
	for (const FPullItem& Item : Plan.Items)
	{
		switch (Item.Status)
		{
		case EPullItemStatus::CopyNew:         ToCopy.Add(&Item); break;
		case EPullItemStatus::SkipExists:      Report.NumSkipped++; break;
		case EPullItemStatus::MissingInSource: break; // reported separately by the UI
		}
	}

	FScopedSlowTask SlowTask(static_cast<float>(ToCopy.Num()),
		LOCTEXT("CopyingAssets", "Copying assets from source library..."));
	if (bShowProgressDialog)
	{
		SlowTask.MakeDialog(/*bShowCancelButton*/true);
	}

	for (const FPullItem* Item : ToCopy)
	{
		SlowTask.EnterProgressFrame(1.f, FText::FromString(Item->PackageName));
		if (bShowProgressDialog && SlowTask.ShouldCancel())
		{
			Report.bCancelled = true;
			break;
		}

		// Belt and braces: never overwrite, even if something appeared after the plan was built.
		if (TargetPackageFileExists(Item->TargetFile))
		{
			Report.NumSkipped++;
			continue;
		}

		// Copy via a temp file and rename into place: an interrupted copy must never leave a
		// truncated .uasset at the final path (the never-overwrite policy would then keep it forever).
		const FString TempFile = Item->TargetFile + TEXT(".appull_tmp");
		uint32 CopyResult = IFileManager::Get().Copy(*TempFile, *Item->SourceFile,
			/*bReplace*/true, /*bEvenIfReadOnly*/true);
		if (CopyResult == COPY_OK
			&& !IFileManager::Get().Move(*Item->TargetFile, *TempFile, /*bReplace*/false, /*bEvenIfReadOnly*/true))
		{
			CopyResult = COPY_Fail;
		}
		if (CopyResult != COPY_OK)
		{
			IFileManager::Get().Delete(*TempFile, /*bRequireExists*/false, /*bEvenIfReadOnly*/true, /*bQuiet*/true);
		}

		if (CopyResult == COPY_OK)
		{
			Report.NumCopied++;
			Report.CopiedTargetFiles.Add(Item->TargetFile);
			Report.CopiedPackageNames.Add(Item->PackageName);
			UE_LOG(LogAssetPuller, Log, TEXT("Copied %s"), *Item->PackageName);
		}
		else
		{
			Report.NumFailed++;
			Report.FailedFiles.Add(Item->PackageName);
			UE_LOG(LogAssetPuller, Error, TEXT("FAILED to copy %s (%s -> %s)"),
				*Item->PackageName, *Item->SourceFile, *Item->TargetFile);
		}
	}

	if (Report.CopiedTargetFiles.Num() > 0)
	{
		RefreshAssetRegistry(Report);
	}

	UE_LOG(LogAssetPuller, Log, TEXT("Pull finished: %d copied, %d skipped (already existed), %d failed%s"),
		Report.NumCopied, Report.NumSkipped, Report.NumFailed,
		Report.bCancelled ? TEXT(" [CANCELLED by user]") : TEXT(""));

	return Report;
}

void FAssetPullExecutor::RefreshAssetRegistry(const FPullReport& Report)
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	// Despite the name this also picks up brand-new files (it force-rescans and broadcasts
	// AssetAdded events), so copied assets show up in the Content Browser immediately.
	AssetRegistry.ScanModifiedAssetFiles(Report.CopiedTargetFiles);
}

#undef LOCTEXT_NAMESPACE
