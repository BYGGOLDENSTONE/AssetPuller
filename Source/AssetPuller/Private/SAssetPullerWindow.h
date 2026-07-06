#pragma once

#include "AssetBrowseModel.h"
#include "AssetDumpIndex.h"
#include "AssetPullerTypes.h"
#include "CoreMinimal.h"
#include "DumpThumbnailCache.h"
#include "Types/SlateEnums.h"   // ESelectInfo
#include "Widgets/SCompoundWidget.h"

class SSearchBox;
class SWidgetSwitcher;
class ITableRow;
class STableViewBase;
template <typename ItemType> class SListView;
template <typename ItemType> class STileView;
template <typename ItemType> class STreeView;

/** Which panel the window is showing. */
enum class EPullerMode : uint8
{
	Search,
	Browse,
};

/** Browse-mode grid type filter (from filename prefix). */
enum class EBrowseChip : uint8
{
	All,
	Static,
	Skeletal,
};

/** Main Asset Puller tab: source settings, name search + visual Browse over the dump library, import flow. */
class SAssetPullerWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAssetPullerWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// Search-mode callbacks
	void OnSearchTextChanged(const FText& NewText);
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FDumpAssetEntry> Entry, const TSharedRef<STableViewBase>& OwnerTable);

	// Shared callbacks
	FReply OnBrowseClicked();
	FReply OnRescanClicked();
	FReply OnSelectAllClicked();
	FReply OnImportSelectedClicked();
	void OnSourcePathCommitted(const FText& NewText, ETextCommit::Type CommitType);

	// Browse-mode callbacks
	EPullerMode GetCurrentMode() const { return CurrentMode; }
	void SetMode(EPullerMode NewMode);
	EBrowseChip GetBrowseChip() const { return BrowseChip; }
	void SetBrowseChip(EBrowseChip Chip);
	void OnBrowseFilterChanged(const FText& NewText);
	TSharedRef<ITableRow> OnGenerateBrowseTile(TSharedPtr<FDumpAssetEntry> Entry, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<ITableRow> OnGenerateBrowseTreeRow(TSharedPtr<FBrowseNode> Node, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetBrowseTreeChildren(TSharedPtr<FBrowseNode> Node, TArray<TSharedPtr<FBrowseNode>>& OutChildren);
	void OnBrowseTreeSelectionChanged(TSharedPtr<FBrowseNode> Node, ESelectInfo::Type SelectInfo);

	// Text bindings
	FText GetSourcePathText() const;
	FText GetIndexStatusText() const;
	FText GetMatchStatusText() const;
	FText GetImportButtonText() const;
	bool IsImportEnabled() const;

	EActiveTimerReturnType OnInitialIndexTimer(double InCurrentTime, float InDeltaTime);
	void RebuildIndex(bool bShowProgress);
	void StartBackgroundRescan();
	static FString GetIndexCacheFilePath();
	void RefreshSearch();
	void RefreshBrowse();
	void RebuildBrowseGrid();
	void RunImport(const TArray<TSharedPtr<FDumpAssetEntry>>& Selected);
	void ShowReport(const FPullReport& Report, const FPullPlan& Plan);

	/** Selection of the panel that's currently visible. */
	int32 GetActiveSelectedCount() const;
	TArray<TSharedPtr<FDumpAssetEntry>> GetActiveSelectedItems() const;

	FAssetDumpIndex Index;
	FDumpThumbnailCache ThumbnailCache;
	FString CurrentQuery;
	int32 TotalMatches = 0;
	TArray<TSharedPtr<FDumpAssetEntry>> FilteredEntries;

	/** Per-session only, always starts off: overwriting existing assets is a deliberate act. */
	bool bUpdateExisting = false;

	/** Invalidates in-flight background scans when the user rescans or changes the path. */
	int32 ScanGeneration = 0;
	bool bScanningInBackground = false;

	// Mode state
	EPullerMode CurrentMode = EPullerMode::Search;

	// Browse state
	EBrowseChip BrowseChip = EBrowseChip::All;
	FString BrowseFilter;
	TArray<FBrowseMesh> BrowseMeshes;                     // every browsable mesh, classified
	TArray<TSharedPtr<FBrowseNode>> BrowseRootNodes;      // pack tree roots
	TSharedPtr<FBrowseNode> SelectedBrowseNode;
	TArray<TSharedPtr<FDumpAssetEntry>> BrowseGridItems;  // current grid contents (filtered)

	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SListView<TSharedPtr<FDumpAssetEntry>>> ResultsList;
	TSharedPtr<SWidgetSwitcher> BodySwitcher;
	TSharedPtr<SSearchBox> BrowseFilterBox;
	TSharedPtr<STreeView<TSharedPtr<FBrowseNode>>> BrowseTree;
	TSharedPtr<STileView<TSharedPtr<FDumpAssetEntry>>> BrowseGrid;

	static constexpr int32 MaxShownResults = 500;
};
