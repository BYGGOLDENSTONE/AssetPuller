#include "SAssetPullerWindow.h"

#include "AssetDependencyResolver.h"
#include "AssetPullerSettings.h"
#include "AssetPullExecutor.h"
#include "Async/Async.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "SPullConfirmDialog.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "AssetPuller"

void SAssetPullerWindow::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(10)
		[
			SNew(SVerticalBox)

			// Source folder row
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("SourceLabel", "Source Content Folder:"))
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SEditableTextBox)
					.Text(this, &SAssetPullerWindow::GetSourcePathText)
					.OnTextCommitted(this, &SAssetPullerWindow::OnSourcePathCommitted)
					.ToolTipText(LOCTEXT("SourceTooltip", "The Content folder of the asset dump project (also editable in Project Settings > Plugins > Asset Puller)"))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("BrowseButton", "Browse..."))
					.OnClicked(this, &SAssetPullerWindow::OnBrowseClicked)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("RescanButton", "Rescan"))
					.ToolTipText(LOCTEXT("RescanTooltip", "Re-index the source folder (use after adding new packs to the library)"))
					.OnClicked(this, &SAssetPullerWindow::OnRescanClicked)
				]
			]

			// Options + index status row
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([]()
					{
						return GetDefault<UAssetPullerSettings>()->bIncludeSoftReferences ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([](ECheckBoxState State)
					{
						GetMutableDefault<UAssetPullerSettings>()->SetIncludeSoftReferences(State == ECheckBoxState::Checked);
					})
					[
						SNew(STextBlock).Text(LOCTEXT("SoftRefsCheck", "Include soft references"))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(16, 0, 0, 0)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]()
					{
						return bUpdateExisting ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
					{
						bUpdateExisting = State == ECheckBoxState::Checked;
					})
					.ToolTipText(LOCTEXT("UpdateTooltip", "Overwrite existing assets whose content differs in the source library. Old files are backed up to Saved/AssetPullerBackups. Maps are never updated."))
					[
						SNew(STextBlock).Text(LOCTEXT("UpdateCheck", "Update existing (overwrite changed)"))
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNullWidget::NullWidget
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &SAssetPullerWindow::GetIndexStatusText)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]

			// Search box
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
			[
				SAssignNew(SearchBox, SSearchBox)
				.HintText(LOCTEXT("SearchHint", "Type an asset name, e.g. SM_Bld_House_01 — separate multiple names with commas"))
				.OnTextChanged(this, &SAssetPullerWindow::OnSearchTextChanged)
			]

			// Results list
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
				.Padding(4)
				[
					SAssignNew(ResultsList, SListView<TSharedPtr<FDumpAssetEntry>>)
					.ListItemsSource(&FilteredEntries)
					.OnGenerateRow(this, &SAssetPullerWindow::OnGenerateRow)
					.SelectionMode(ESelectionMode::Multi)
				]
			]

			// Bottom bar
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &SAssetPullerWindow::GetMatchStatusText)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("SelectAllButton", "Select All"))
					.OnClicked(this, &SAssetPullerWindow::OnSelectAllClicked)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
					.Text(this, &SAssetPullerWindow::GetImportButtonText)
					.IsEnabled(this, &SAssetPullerWindow::IsImportEnabled)
					.OnClicked(this, &SAssetPullerWindow::OnImportSelectedClicked)
				]
			]
		]
	];

	// Defer the first index load one frame so the tab appears instantly.
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SAssetPullerWindow::OnInitialIndexTimer));
}

EActiveTimerReturnType SAssetPullerWindow::OnInitialIndexTimer(double, float)
{
	// Instant open on big libraries: last session's index loads from cache immediately,
	// while a fresh scan runs on a worker thread and swaps in when done.
	const FString SourceDir = GetDefault<UAssetPullerSettings>()->GetSourceContentDir();
	if (Index.LoadFromCache(GetIndexCacheFilePath(), SourceDir))
	{
		RefreshSearch();
	}
	StartBackgroundRescan();
	return EActiveTimerReturnType::Stop;
}

FString SAssetPullerWindow::GetIndexCacheFilePath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("AssetPuller") / TEXT("IndexCache.bin");
}

void SAssetPullerWindow::StartBackgroundRescan()
{
	const FString SourceDir = GetDefault<UAssetPullerSettings>()->GetSourceContentDir();
	if (SourceDir.IsEmpty())
	{
		Index = FAssetDumpIndex();
		RefreshSearch();
		return;
	}

	const int32 Generation = ++ScanGeneration;
	bScanningInBackground = true;
	TWeakPtr<SAssetPullerWindow> WeakSelf = StaticCastSharedRef<SAssetPullerWindow>(AsShared());

	Async(EAsyncExecution::ThreadPool, [WeakSelf, Generation, SourceDir]()
	{
		// Pure filesystem work — safe off the game thread. The cache write happens here
		// too, while this thread still exclusively owns the index (no game-thread hitch).
		TSharedPtr<FAssetDumpIndex, ESPMode::ThreadSafe> FreshIndex = MakeShared<FAssetDumpIndex, ESPMode::ThreadSafe>();
		if (FreshIndex->Build(SourceDir))
		{
			FreshIndex->SaveToCache(GetIndexCacheFilePath());
		}

		AsyncTask(ENamedThreads::GameThread, [WeakSelf, Generation, FreshIndex]()
		{
			TSharedPtr<SAssetPullerWindow> Self = WeakSelf.Pin();
			if (!Self.IsValid() || Self->ScanGeneration != Generation)
			{
				return; // window closed, or a newer scan superseded this one
			}
			Self->bScanningInBackground = false;
			Self->Index = MoveTemp(*FreshIndex);
			Self->RefreshSearch();
		});
	});
}

void SAssetPullerWindow::RebuildIndex(bool bShowProgress)
{
	const FString SourceDir = GetDefault<UAssetPullerSettings>()->GetSourceContentDir();
	++ScanGeneration;   // cancel any in-flight background scan result
	bScanningInBackground = false;

	FScopedSlowTask SlowTask(1.f, LOCTEXT("Indexing", "Indexing source asset library..."), bShowProgress);
	if (bShowProgress)
	{
		SlowTask.MakeDialog();
	}
	SlowTask.EnterProgressFrame(1.f);

	if (Index.Build(SourceDir))
	{
		Index.SaveToCache(GetIndexCacheFilePath());
	}
	ThumbnailCache.Reset();   // source may have changed — don't show stale images
	RefreshSearch();
}

void SAssetPullerWindow::RefreshSearch()
{
	FilteredEntries = Index.Search(CurrentQuery, MaxShownResults, TotalMatches);
	if (ResultsList.IsValid())
	{
		ResultsList->RequestListRefresh();
	}
}

void SAssetPullerWindow::OnSearchTextChanged(const FText& NewText)
{
	CurrentQuery = NewText.ToString();
	RefreshSearch();
}

TSharedRef<ITableRow> SAssetPullerWindow::OnGenerateRow(TSharedPtr<FDumpAssetEntry> Entry, const TSharedRef<STableViewBase>& OwnerTable)
{
	const FString Folder = FPaths::GetPath(Entry->RelPath);

	// Rows are virtualized, so only visible entries ever load their thumbnail (cached).
	// The lambda keeps a shared reference so cache eviction can't free a brush mid-display.
	TSharedPtr<FSlateDynamicImageBrush> Thumbnail = ThumbnailCache.GetThumbnail(*Entry);

	return SNew(STableRow<TSharedPtr<FDumpAssetEntry>>, OwnerTable)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 2).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(36).HeightOverride(36)
			[
				Thumbnail.IsValid()
					? SNew(SImage).Image_Lambda([Thumbnail]() -> const FSlateBrush* { return Thumbnail.Get(); })
					: SNew(SImage).Visibility(EVisibility::Hidden)
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 2).VAlign(VAlign_Center)
		[
			SNew(SBox).MinDesiredWidth(320)
			[
				SNew(STextBlock).Text(FText::FromString(Entry->AssetName))
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(4, 2).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Folder))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 2).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(Entry->bIsMap ? LOCTEXT("TypeMap", "Map") : FText::GetEmpty())
			.ColorAndOpacity(FStyleColors::Warning)
		]
	];
}

FReply SAssetPullerWindow::OnBrowseClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		void* ParentWindowHandle = nullptr;
		TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
		if (ParentWindow.IsValid() && ParentWindow->GetNativeWindow().IsValid())
		{
			ParentWindowHandle = ParentWindow->GetNativeWindow()->GetOSWindowHandle();
		}

		FString PickedDir;
		if (DesktopPlatform->OpenDirectoryDialog(ParentWindowHandle,
			LOCTEXT("BrowseTitle", "Choose the source project's Content folder").ToString(),
			GetDefault<UAssetPullerSettings>()->GetSourceContentDir(), PickedDir))
		{
			GetMutableDefault<UAssetPullerSettings>()->SetSourceContentDir(PickedDir);
			RebuildIndex(/*bShowProgress*/true);
		}
	}
	return FReply::Handled();
}

FReply SAssetPullerWindow::OnRescanClicked()
{
	RebuildIndex(/*bShowProgress*/true);
	return FReply::Handled();
}

FReply SAssetPullerWindow::OnSelectAllClicked()
{
	if (ResultsList.IsValid())
	{
		ResultsList->ClearSelection();
		ResultsList->SetItemSelection(FilteredEntries, true);
	}
	return FReply::Handled();
}

void SAssetPullerWindow::OnSourcePathCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter || CommitType == ETextCommit::OnUserMovedFocus)
	{
		FString NewDir = NewText.ToString().TrimStartAndEnd();
		FPaths::NormalizeDirectoryName(NewDir);   // compare like-for-like, or Enter+focus-loss re-indexes twice
		if (NewDir != GetDefault<UAssetPullerSettings>()->GetSourceContentDir())
		{
			GetMutableDefault<UAssetPullerSettings>()->SetSourceContentDir(NewDir);
			RebuildIndex(/*bShowProgress*/true);
		}
	}
}

FText SAssetPullerWindow::GetSourcePathText() const
{
	return FText::FromString(GetDefault<UAssetPullerSettings>()->GetSourceContentDir());
}

FText SAssetPullerWindow::GetIndexStatusText() const
{
	if (GetDefault<UAssetPullerSettings>()->GetSourceContentDir().IsEmpty())
	{
		return LOCTEXT("IndexNoPath", "Set the source library's Content folder above to get started");
	}
	if (!Index.IsBuilt())
	{
		return bScanningInBackground
			? LOCTEXT("IndexScanning", "Scanning source library...")
			: LOCTEXT("IndexEmpty", "No assets found — check the source folder path, then press Rescan");
	}
	return bScanningInBackground
		? FText::Format(LOCTEXT("IndexStatusRefreshing", "{0} assets in library (refreshing...)"), FText::AsNumber(Index.Num()))
		: FText::Format(LOCTEXT("IndexStatus", "{0} assets in library"), FText::AsNumber(Index.Num()));
}

FText SAssetPullerWindow::GetMatchStatusText() const
{
	if (CurrentQuery.TrimStartAndEnd().IsEmpty())
	{
		return FText::GetEmpty();
	}
	if (TotalMatches == 0)
	{
		return LOCTEXT("NoMatches", "No matches — check the spelling, or press Rescan if the library changed");
	}
	if (TotalMatches > FilteredEntries.Num())
	{
		return FText::Format(LOCTEXT("MatchesCapped", "Showing first {0} of {1} matches — type more to narrow down"),
			FText::AsNumber(FilteredEntries.Num()), FText::AsNumber(TotalMatches));
	}
	const int32 NumSelected = ResultsList.IsValid() ? ResultsList->GetNumItemsSelected() : 0;
	return FText::Format(LOCTEXT("Matches", "{0} match(es), {1} selected"),
		FText::AsNumber(TotalMatches), FText::AsNumber(NumSelected));
}

FText SAssetPullerWindow::GetImportButtonText() const
{
	const int32 NumSelected = ResultsList.IsValid() ? ResultsList->GetNumItemsSelected() : 0;
	return NumSelected > 0
		? FText::Format(LOCTEXT("ImportSelectedN", "Import Selected ({0})"), FText::AsNumber(NumSelected))
		: LOCTEXT("ImportSelected", "Import Selected");
}

bool SAssetPullerWindow::IsImportEnabled() const
{
	return ResultsList.IsValid() && ResultsList->GetNumItemsSelected() > 0;
}

FReply SAssetPullerWindow::OnImportSelectedClicked()
{
	if (ResultsList.IsValid())
	{
		RunImport(ResultsList->GetSelectedItems());
	}
	return FReply::Handled();
}

void SAssetPullerWindow::RunImport(const TArray<TSharedPtr<FDumpAssetEntry>>& Selected)
{
	if (Selected.Num() == 0)
	{
		return;
	}

	FAssetDependencyResolver::FParams Params;
	Params.SourceContentDir = GetDefault<UAssetPullerSettings>()->GetSourceContentDir();
	Params.TargetContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
	FPaths::NormalizeDirectoryName(Params.TargetContentDir);
	Params.bIncludeSoftReferences = GetDefault<UAssetPullerSettings>()->bIncludeSoftReferences;
	Params.bUpdateExisting = bUpdateExisting;

	// Also rejects nested setups (source inside this project's Content or vice versa),
	// which would make the project pull itself.
	if (FPaths::IsUnderDirectory(Params.SourceContentDir, Params.TargetContentDir) ||
		FPaths::IsUnderDirectory(Params.TargetContentDir, Params.SourceContentDir))
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("SameProjectError", "The source folder overlaps this project's own Content folder. Point it at a different project's Content (e.g. the asset dump project)."),
			LOCTEXT("AssetPullerTitle", "Asset Puller"));
		return;
	}

	FPullPlan Plan;
	{
		FScopedSlowTask SlowTask(1.f, LOCTEXT("Resolving", "Resolving dependencies..."));
		SlowTask.MakeDialog();
		Plan = FAssetDependencyResolver::BuildPlan(Selected, Params, [&SlowTask](const FString& PackageName)
		{
			// Zero-work frame: updates the dialog text and keeps the UI alive without advancing the bar.
			SlowTask.EnterProgressFrame(0.f, FText::Format(LOCTEXT("ResolvingPackage", "Reading {0}"), FText::FromString(PackageName)));
		});
	}

	if (!SPullConfirmDialog::ShowModal(Plan, AsShared()))
	{
		return;
	}

	const FPullReport Report = FAssetPullExecutor::Execute(Plan, /*bShowProgressDialog*/true);
	ShowReport(Report, Plan);
	RefreshSearch();
}

void SAssetPullerWindow::ShowReport(const FPullReport& Report, const FPullPlan& Plan)
{
	const bool bSuccess = Report.NumFailed == 0 && !Report.bCancelled;

	FText NotificationText = Report.NumUpdated > 0
		? FText::Format(LOCTEXT("NotifyResultWithUpdates", "Asset Puller: {0} copied, {1} updated, {2} skipped"),
			FText::AsNumber(Report.NumCopied), FText::AsNumber(Report.NumUpdated), FText::AsNumber(Report.NumSkipped))
		: FText::Format(LOCTEXT("NotifyResult", "Asset Puller: {0} copied, {1} skipped"),
			FText::AsNumber(Report.NumCopied), FText::AsNumber(Report.NumSkipped));
	FNotificationInfo Info(NotificationText);
	Info.ExpireDuration = 5.0f;
	Info.bUseSuccessFailIcons = true;
	TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
	if (Notification.IsValid())
	{
		Notification->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}

	if (Report.NumFailed > 0 || Report.bCancelled || Plan.NumMissing > 0 || !Report.BackupDir.IsEmpty()
		|| Report.SkippedDirtyPackages.Num() > 0)
	{
		FString Details;
		if (Report.SkippedDirtyPackages.Num() > 0)
		{
			Details += FString::Printf(TEXT("%d asset(s) were NOT updated because they have unsaved changes in this editor — save them and run the update again:\n"),
				Report.SkippedDirtyPackages.Num());
			for (int32 i = 0; i < Report.SkippedDirtyPackages.Num() && i < 10; ++i)
			{
				Details += TEXT("  ") + Report.SkippedDirtyPackages[i] + TEXT("\n");
			}
			Details += TEXT("\n");
		}
		if (!Report.BackupDir.IsEmpty())
		{
			Details += FString::Printf(TEXT("Overwritten files were backed up to:\n%s\n\n"), *Report.BackupDir);
		}
		if (Report.bCancelled)
		{
			Details += TEXT("The operation was cancelled before finishing.\n\n");
		}
		if (Report.NumFailed > 0)
		{
			Details += FString::Printf(TEXT("%d file(s) FAILED to copy:\n"), Report.NumFailed);
			for (int32 i = 0; i < Report.FailedFiles.Num() && i < 10; ++i)
			{
				Details += TEXT("  ") + Report.FailedFiles[i] + TEXT("\n");
			}
			if (Report.FailedFiles.Num() > 10)
			{
				Details += FString::Printf(TEXT("  ... and %d more (see Output Log)\n"), Report.FailedFiles.Num() - 10);
			}
			Details += TEXT("\n");
		}
		if (Plan.NumMissing > 0)
		{
			Details += FString::Printf(TEXT("%d referenced asset(s) were missing in the source library (see Output Log, category LogAssetPuller)."), Plan.NumMissing);
		}

		FMessageDialog::Open(EAppMsgType::Ok,
			FText::Format(LOCTEXT("ReportWithIssues", "Finished: {0} copied, {1} updated, {2} skipped, {3} failed.\n\n{4}"),
				FText::AsNumber(Report.NumCopied), FText::AsNumber(Report.NumUpdated), FText::AsNumber(Report.NumSkipped),
				FText::AsNumber(Report.NumFailed), FText::FromString(Details)),
			LOCTEXT("AssetPullerTitle", "Asset Puller"));
	}
}

#undef LOCTEXT_NAMESPACE
