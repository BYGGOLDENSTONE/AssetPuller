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
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STileView.h"
#include "Widgets/Views/STreeView.h"

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

			// Mode toggle (Search / Browse)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SSegmentedControl<EPullerMode>)
					.Value(this, &SAssetPullerWindow::GetCurrentMode)
					.OnValueChanged(this, &SAssetPullerWindow::SetMode)
					+ SSegmentedControl<EPullerMode>::Slot(EPullerMode::Search)
						.Text(LOCTEXT("SearchModeTab", "Search"))
						.ToolTip(LOCTEXT("SearchModeTip", "Find assets by typing their name"))
					+ SSegmentedControl<EPullerMode>::Slot(EPullerMode::Browse)
						.Text(LOCTEXT("BrowseModeTab", "Browse"))
						.ToolTip(LOCTEXT("BrowseModeTip", "Browse meshes visually by pack and category"))
				]
			]

			// Body: Search panel or Browse panel
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SAssignNew(BodySwitcher, SWidgetSwitcher)
				.WidgetIndex(0)

				// Index 0 — Search panel
				+ SWidgetSwitcher::Slot()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
					[
						SAssignNew(SearchBox, SSearchBox)
						.HintText(LOCTEXT("SearchHint", "Type an asset name, e.g. SM_Bld_House_01 — separate multiple names with commas"))
						.OnTextChanged(this, &SAssetPullerWindow::OnSearchTextChanged)
					]
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
				]

				// Index 1 — Browse panel
				+ SWidgetSwitcher::Slot()
				[
					SNew(SVerticalBox)

					// Type chips + name filter
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SSegmentedControl<EBrowseChip>)
							.Value(this, &SAssetPullerWindow::GetBrowseChip)
							.OnValueChanged(this, &SAssetPullerWindow::SetBrowseChip)
							+ SSegmentedControl<EBrowseChip>::Slot(EBrowseChip::All)
								.Text(LOCTEXT("ChipAll", "All"))
							+ SSegmentedControl<EBrowseChip>::Slot(EBrowseChip::Static)
								.Text(LOCTEXT("ChipStatic", "Static Meshes"))
								.ToolTip(LOCTEXT("ChipStaticTip", "SM_ / geo_ static meshes"))
							+ SSegmentedControl<EBrowseChip>::Slot(EBrowseChip::Skeletal)
								.Text(LOCTEXT("ChipSkeletal", "Skeletal"))
								.ToolTip(LOCTEXT("ChipSkeletalTip", "SK_ skeletal / character meshes"))
						]
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(8, 0, 0, 0)
						[
							SAssignNew(BrowseFilterBox, SSearchBox)
							.HintText(LOCTEXT("BrowseFilterHint", "Filter meshes by name in this view..."))
							.OnTextChanged(this, &SAssetPullerWindow::OnBrowseFilterChanged)
						]
					]

					// Left pack/category tree | right thumbnail grid
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SNew(SSplitter)
						.Orientation(Orient_Horizontal)
						+ SSplitter::Slot().Value(0.28f)
						[
							SNew(SBorder)
							.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
							.Padding(4)
							[
								SAssignNew(BrowseTree, STreeView<TSharedPtr<FBrowseNode>>)
								.TreeItemsSource(&BrowseRootNodes)
								.OnGenerateRow(this, &SAssetPullerWindow::OnGenerateBrowseTreeRow)
								.OnGetChildren(this, &SAssetPullerWindow::OnGetBrowseTreeChildren)
								.OnSelectionChanged(this, &SAssetPullerWindow::OnBrowseTreeSelectionChanged)
								.SelectionMode(ESelectionMode::Single)
							]
						]
						+ SSplitter::Slot().Value(0.72f)
						[
							SNew(SBorder)
							.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
							.Padding(4)
							[
								SAssignNew(BrowseGrid, STileView<TSharedPtr<FDumpAssetEntry>>)
								.ListItemsSource(&BrowseGridItems)
								.OnGenerateTile(this, &SAssetPullerWindow::OnGenerateBrowseTile)
								.ItemWidth(150.f)
								.ItemHeight(188.f)
								.ItemAlignment(EListItemAlignment::LeftAligned)
								.SelectionMode(ESelectionMode::Multi)
							]
						]
					]
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
		RefreshBrowse();
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
		RefreshBrowse();
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
			Self->RefreshBrowse();
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
	RefreshBrowse();
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
	if (CurrentMode == EPullerMode::Browse)
	{
		if (BrowseGrid.IsValid())
		{
			BrowseGrid->ClearSelection();
			BrowseGrid->SetItemSelection(BrowseGridItems, true);
		}
	}
	else if (ResultsList.IsValid())
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
	if (CurrentMode == EPullerMode::Browse)
	{
		if (!SelectedBrowseNode.IsValid() && BrowseFilter.TrimStartAndEnd().IsEmpty())
		{
			return Index.IsBuilt()
				? LOCTEXT("BrowseHint", "Select a pack or category on the left to see its meshes")
				: FText::GetEmpty();
		}
		return FText::Format(LOCTEXT("BrowseCount", "{0} mesh(es), {1} selected"),
			FText::AsNumber(BrowseGridItems.Num()), FText::AsNumber(GetActiveSelectedCount()));
	}

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
	const int32 NumSelected = GetActiveSelectedCount();
	return NumSelected > 0
		? FText::Format(LOCTEXT("ImportSelectedN", "Import Selected ({0})"), FText::AsNumber(NumSelected))
		: LOCTEXT("ImportSelected", "Import Selected");
}

bool SAssetPullerWindow::IsImportEnabled() const
{
	return GetActiveSelectedCount() > 0;
}

FReply SAssetPullerWindow::OnImportSelectedClicked()
{
	RunImport(GetActiveSelectedItems());
	return FReply::Handled();
}

int32 SAssetPullerWindow::GetActiveSelectedCount() const
{
	if (CurrentMode == EPullerMode::Browse)
	{
		return BrowseGrid.IsValid() ? BrowseGrid->GetNumItemsSelected() : 0;
	}
	return ResultsList.IsValid() ? ResultsList->GetNumItemsSelected() : 0;
}

TArray<TSharedPtr<FDumpAssetEntry>> SAssetPullerWindow::GetActiveSelectedItems() const
{
	if (CurrentMode == EPullerMode::Browse)
	{
		return BrowseGrid.IsValid() ? BrowseGrid->GetSelectedItems() : TArray<TSharedPtr<FDumpAssetEntry>>();
	}
	return ResultsList.IsValid() ? ResultsList->GetSelectedItems() : TArray<TSharedPtr<FDumpAssetEntry>>();
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

void SAssetPullerWindow::SetMode(EPullerMode NewMode)
{
	CurrentMode = NewMode;
	if (BodySwitcher.IsValid())
	{
		BodySwitcher->SetActiveWidgetIndex(NewMode == EPullerMode::Browse ? 1 : 0);
	}
}

void SAssetPullerWindow::SetBrowseChip(EBrowseChip Chip)
{
	BrowseChip = Chip;
	RebuildBrowseGrid();
}

void SAssetPullerWindow::OnBrowseFilterChanged(const FText& NewText)
{
	BrowseFilter = NewText.ToString();
	RebuildBrowseGrid();
}

void SAssetPullerWindow::RefreshBrowse()
{
	// Rebuild the mesh list + pack/category tree from the current index (game thread only).
	SelectedBrowseNode.Reset();
	AssetBrowseModel::Build(Index.GetEntries(), BrowseMeshes, BrowseRootNodes);
	if (BrowseTree.IsValid())
	{
		BrowseTree->ClearSelection();
		BrowseTree->RequestTreeRefresh();
	}
	RebuildBrowseGrid();
}

void SAssetPullerWindow::RebuildBrowseGrid()
{
	BrowseGridItems.Reset();

	const FString FilterLower = BrowseFilter.TrimStartAndEnd().ToLower();
	const bool bHasFilter = !FilterLower.IsEmpty();
	const bool bHasNode = SelectedBrowseNode.IsValid();

	// Nothing selected and no filter: leave the grid empty (a 25k-tile "everything" view is
	// rarely what's wanted, and the status bar prompts the user to pick a pack).
	if (bHasNode || bHasFilter)
	{
		for (const FBrowseMesh& Mesh : BrowseMeshes)
		{
			if (bHasNode)
			{
				if (Mesh.PackKey != SelectedBrowseNode->PackKey)
				{
					continue;
				}
				if (!SelectedBrowseNode->bIsPack && Mesh.Category != SelectedBrowseNode->Category)
				{
					continue;
				}
			}
			if (BrowseChip == EBrowseChip::Static && Mesh.Kind != EBrowseMeshKind::Static)
			{
				continue;
			}
			if (BrowseChip == EBrowseChip::Skeletal && Mesh.Kind != EBrowseMeshKind::Skeletal)
			{
				continue;
			}
			if (bHasFilter && !Mesh.Entry->AssetNameLower.Contains(FilterLower))
			{
				continue;
			}
			BrowseGridItems.Add(Mesh.Entry);
		}

		BrowseGridItems.Sort([](const TSharedPtr<FDumpAssetEntry>& A, const TSharedPtr<FDumpAssetEntry>& B)
		{
			return A->AssetName < B->AssetName;
		});
	}

	if (BrowseGrid.IsValid())
	{
		BrowseGrid->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SAssetPullerWindow::OnGenerateBrowseTile(TSharedPtr<FDumpAssetEntry> Entry, const TSharedRef<STableViewBase>& OwnerTable)
{
	// Tiles are virtualized, so only visible meshes ever load their thumbnail (cached).
	// The lambda holds a shared reference so cache eviction can't free a brush mid-display.
	TSharedPtr<FSlateDynamicImageBrush> Thumbnail = Entry.IsValid() ? ThumbnailCache.GetThumbnail(*Entry) : nullptr;

	return SNew(STableRow<TSharedPtr<FDumpAssetEntry>>, OwnerTable)
		.Padding(2.f)
		.ToolTipText(Entry.IsValid() ? FText::FromString(Entry->RelPath) : FText::GetEmpty())
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(2)
			[
				SNew(SBox).WidthOverride(128).HeightOverride(128)
				[
					SNew(SOverlay)
					// Neutral backdrop so dark/thin meshes stay visible against the dark theme.
					+ SOverlay::Slot()
					[
						SNew(SColorBlock).Color(FLinearColor(0.34f, 0.34f, 0.36f, 1.f))
					]
					+ SOverlay::Slot()
					[
						Thumbnail.IsValid()
							? SNew(SImage).Image_Lambda([Thumbnail]() -> const FSlateBrush* { return Thumbnail.Get(); })
							: SNew(SImage).Visibility(EVisibility::Hidden)
					]
				]
			]
			+ SVerticalBox::Slot().FillHeight(1.f).HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(2, 0)
			[
				SNew(STextBlock)
				.Text(Entry.IsValid() ? FText::FromString(Entry->AssetName) : FText::GetEmpty())
				.Justification(ETextJustify::Center)
				.AutoWrapText(true)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]
		];
}

TSharedRef<ITableRow> SAssetPullerWindow::OnGenerateBrowseTreeRow(TSharedPtr<FBrowseNode> Node, const TSharedRef<STableViewBase>& OwnerTable)
{
	const bool bIsPack = Node.IsValid() && Node->bIsPack;
	const FText Label = Node.IsValid()
		? FText::Format(LOCTEXT("BrowseNodeLabel", "{0} ({1})"), FText::FromString(Node->DisplayName), FText::AsNumber(Node->Count))
		: FText::GetEmpty();

	return SNew(STableRow<TSharedPtr<FBrowseNode>>, OwnerTable)
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(FCoreStyle::GetDefaultFontStyle(bIsPack ? "Bold" : "Regular", 9))
		];
}

void SAssetPullerWindow::OnGetBrowseTreeChildren(TSharedPtr<FBrowseNode> Node, TArray<TSharedPtr<FBrowseNode>>& OutChildren)
{
	if (Node.IsValid())
	{
		OutChildren = Node->Children;
	}
}

void SAssetPullerWindow::OnBrowseTreeSelectionChanged(TSharedPtr<FBrowseNode> Node, ESelectInfo::Type /*SelectInfo*/)
{
	SelectedBrowseNode = Node;
	RebuildBrowseGrid();
}

#undef LOCTEXT_NAMESPACE
