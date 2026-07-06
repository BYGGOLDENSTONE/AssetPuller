#include "AssetPullExecutor.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "PackageTools.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

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

	TArray<const FPullItem*> ToWrite;
	for (const FPullItem& Item : Plan.Items)
	{
		switch (Item.Status)
		{
		case EPullItemStatus::CopyNew:         ToWrite.Add(&Item); break;
		case EPullItemStatus::UpdateExisting:  ToWrite.Add(&Item); break;
		case EPullItemStatus::SkipExists:      Report.NumSkipped++; break;
		case EPullItemStatus::MissingInSource: break; // reported separately by the UI
		}
	}

	// Updated packages that are LOADED in this editor hold an open file handle through
	// their linker — release it before touching the file (same pattern as the engine's
	// source-control revert flow), and remember them so they can be reloaded afterwards.
	// Packages with UNSAVED changes are refused entirely: force-reloading them would
	// silently throw the user's edits away (and the backup would predate those edits).
	TMap<FString, UPackage*> LoadedPackagesByName;
	TSet<FString> DirtyPackageNames;
	if (!IsRunningCommandlet())
	{
		for (const FPullItem* Item : ToWrite)
		{
			if (Item->Status == EPullItemStatus::UpdateExisting)
			{
				if (UPackage* Loaded = FindPackage(nullptr, *Item->PackageName))
				{
					if (Loaded->IsDirty())
					{
						DirtyPackageNames.Add(Item->PackageName);
						continue;
					}
					ResetLoaders(Loaded);
					LoadedPackagesByName.Add(Item->PackageName, Loaded);
				}
			}
		}
	}

	FScopedSlowTask SlowTask(static_cast<float>(ToWrite.Num()),
		LOCTEXT("CopyingAssets", "Copying assets from source library..."));
	if (bShowProgressDialog)
	{
		SlowTask.MakeDialog(/*bShowCancelButton*/true);
	}

	for (const FPullItem* Item : ToWrite)
	{
		SlowTask.EnterProgressFrame(1.f, FText::FromString(Item->PackageName));
		if (bShowProgressDialog && SlowTask.ShouldCancel())
		{
			Report.bCancelled = true;
			break;
		}

		const bool bIsUpdate = Item->Status == EPullItemStatus::UpdateExisting;

		if (bIsUpdate && DirtyPackageNames.Contains(Item->PackageName))
		{
			Report.NumSkipped++;
			Report.SkippedDirtyPackages.Add(Item->PackageName);
			UE_LOG(LogAssetPuller, Warning, TEXT("NOT updating %s: it has unsaved changes in this editor — save it and run the update again."),
				*Item->PackageName);
			continue;
		}

		// Belt and braces: never overwrite outside update mode, even if something appeared
		// after the plan was built.
		if (!bIsUpdate && TargetPackageFileExists(Item->TargetFile))
		{
			Report.NumSkipped++;
			continue;
		}

		// Updates keep the old file: Saved/AssetPullerBackups/<timestamp>/<relative path>.
		if (bIsUpdate)
		{
			if (Report.BackupDir.IsEmpty())
			{
				Report.BackupDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir())
					/ TEXT("AssetPullerBackups") / FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
			}
			const FString BackupFile = Report.BackupDir / Item->RelPathWithExt;
			if (IFileManager::Get().Copy(*BackupFile, *Item->TargetFile, /*bReplace*/true, /*bEvenIfReadOnly*/true) != COPY_OK)
			{
				Report.NumFailed++;
				Report.FailedFiles.Add(Item->PackageName + TEXT(" (backup failed — not touched)"));
				UE_LOG(LogAssetPuller, Error, TEXT("FAILED to back up %s — leaving it untouched"), *Item->PackageName);
				continue;
			}
		}

		// Copy via a temp file and rename into place: an interrupted copy must never leave a
		// truncated .uasset at the final path (the never-overwrite policy would then keep it forever).
		const FString TempFile = Item->TargetFile + TEXT(".appull_tmp");
		uint32 CopyResult = IFileManager::Get().Copy(*TempFile, *Item->SourceFile,
			/*bReplace*/true, /*bEvenIfReadOnly*/true);
		if (bIsUpdate)
		{
			// Move's pre-delete fails on read-only destinations; the backup was taken above.
			FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Item->TargetFile, false);
		}
		if (CopyResult == COPY_OK
			&& !IFileManager::Get().Move(*Item->TargetFile, *TempFile, /*bReplace*/bIsUpdate, /*bEvenIfReadOnly*/true))
		{
			CopyResult = COPY_Fail;
		}
		if (CopyResult != COPY_OK)
		{
			IFileManager::Get().Delete(*TempFile, /*bRequireExists*/false, /*bEvenIfReadOnly*/true, /*bQuiet*/true);

			// A failed replace can leave the target DELETED (Move pre-deletes the destination).
			// Put the backup straight back so a failed update never costs the user the asset.
			if (bIsUpdate && !IFileManager::Get().FileExists(*Item->TargetFile))
			{
				const FString BackupFile = Report.BackupDir / Item->RelPathWithExt;
				if (IFileManager::Get().Copy(*Item->TargetFile, *BackupFile, /*bReplace*/false, /*bEvenIfReadOnly*/true) == COPY_OK)
				{
					UE_LOG(LogAssetPuller, Warning, TEXT("Update of %s failed — the previous file was restored from backup."), *Item->PackageName);
				}
				else
				{
					UE_LOG(LogAssetPuller, Error, TEXT("Update of %s failed AND automatic restore failed — restore it manually from %s"),
						*Item->PackageName, *BackupFile);
				}
			}
		}

		if (CopyResult == COPY_OK)
		{
			Report.CopiedTargetFiles.Add(Item->TargetFile);
			if (bIsUpdate)
			{
				Report.NumUpdated++;
				Report.UpdatedPackageNames.Add(Item->PackageName);
				UE_LOG(LogAssetPuller, Log, TEXT("Updated %s"), *Item->PackageName);
			}
			else
			{
				Report.NumCopied++;
				Report.CopiedPackageNames.Add(Item->PackageName);
				UE_LOG(LogAssetPuller, Log, TEXT("Copied %s"), *Item->PackageName);
			}
		}
		else
		{
			Report.NumFailed++;
			Report.FailedFiles.Add(Item->PackageName);
			UE_LOG(LogAssetPuller, Error, TEXT("FAILED to copy %s (%s -> %s)"),
				*Item->PackageName, *Item->SourceFile, *Item->TargetFile);
		}
	}

	// Refresh the in-memory version of updated packages that were loaded.
	if (LoadedPackagesByName.Num() > 0)
	{
		TArray<UPackage*> PackagesToReload;
		for (const FString& UpdatedName : Report.UpdatedPackageNames)
		{
			if (UPackage** Loaded = LoadedPackagesByName.Find(UpdatedName))
			{
				PackagesToReload.Add(*Loaded);
			}
		}
		if (PackagesToReload.Num() > 0)
		{
			FText ReloadError;
			UPackageTools::ReloadPackages(PackagesToReload, ReloadError, EReloadPackagesInteractionMode::AssumePositive);
			if (!ReloadError.IsEmpty())
			{
				UE_LOG(LogAssetPuller, Warning, TEXT("Reload after update: %s"), *ReloadError.ToString());
			}
		}
	}

	if (Report.CopiedTargetFiles.Num() > 0)
	{
		RefreshAssetRegistry(Report);
	}

	UE_LOG(LogAssetPuller, Log, TEXT("Pull finished: %d copied, %d updated, %d skipped (already existed), %d failed%s%s"),
		Report.NumCopied, Report.NumUpdated, Report.NumSkipped, Report.NumFailed,
		Report.bCancelled ? TEXT(" [CANCELLED by user]") : TEXT(""),
		Report.BackupDir.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" | backups: %s"), *Report.BackupDir));

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
