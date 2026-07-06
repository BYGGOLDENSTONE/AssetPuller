#pragma once

#include "AssetPullerTypes.h"
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/** Modal "you are about to copy N assets" confirmation dialog. */
class SPullConfirmDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPullConfirmDialog) {}
		SLATE_ARGUMENT(const FPullPlan*, Plan)
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Opens the dialog modally. Returns true when the user confirms the import. */
	static bool ShowModal(const FPullPlan& Plan, const TSharedPtr<SWidget>& ParentWidget);

private:
	TSharedRef<class ITableRow> OnGenerateRow(TSharedPtr<FPullItem> Item, const TSharedRef<class STableViewBase>& OwnerTable);
	FReply OnImportClicked();
	FReply OnCancelClicked();

	const FPullPlan* Plan = nullptr;
	TSharedPtr<SWindow> ParentWindow;
	TArray<TSharedPtr<FPullItem>> ListItems;
	bool bConfirmed = false;
};
