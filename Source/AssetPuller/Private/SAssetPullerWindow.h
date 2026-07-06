#pragma once

#include "AssetDumpIndex.h"
#include "AssetPullerTypes.h"
#include "CoreMinimal.h"
#include "DumpThumbnailCache.h"
#include "Widgets/SCompoundWidget.h"

class SSearchBox;
template <typename ItemType> class SListView;
class ITableRow;
class STableViewBase;

/** Main Asset Puller tab: source settings, live search over the dump library, import flow. */
class SAssetPullerWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAssetPullerWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// UI callbacks
	void OnSearchTextChanged(const FText& NewText);
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FDumpAssetEntry> Entry, const TSharedRef<STableViewBase>& OwnerTable);
	FReply OnBrowseClicked();
	FReply OnRescanClicked();
	FReply OnSelectAllClicked();
	FReply OnImportSelectedClicked();
	void OnSourcePathCommitted(const FText& NewText, ETextCommit::Type CommitType);

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
	void RunImport(const TArray<TSharedPtr<FDumpAssetEntry>>& Selected);
	void ShowReport(const FPullReport& Report, const FPullPlan& Plan);

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

	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SListView<TSharedPtr<FDumpAssetEntry>>> ResultsList;

	static constexpr int32 MaxShownResults = 500;
};
