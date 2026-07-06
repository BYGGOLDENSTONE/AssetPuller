#include "SPullConfirmDialog.h"

#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "AssetPuller"

namespace
{
	FText StatusText(const FPullItem& Item)
	{
		switch (Item.Status)
		{
		case EPullItemStatus::CopyNew:         return LOCTEXT("StatusCopy", "Copy");
		case EPullItemStatus::UpdateExisting:  return LOCTEXT("StatusUpdate", "Overwrite");
		case EPullItemStatus::SkipExists:      return LOCTEXT("StatusSkip", "Skip (exists)");
		case EPullItemStatus::MissingInSource: return LOCTEXT("StatusMissing", "MISSING in source");
		}
		return FText::GetEmpty();
	}

	FSlateColor StatusColor(const FPullItem& Item)
	{
		switch (Item.Status)
		{
		case EPullItemStatus::CopyNew:         return FStyleColors::AccentGreen;
		case EPullItemStatus::UpdateExisting:  return FStyleColors::Warning;
		case EPullItemStatus::SkipExists:      return FSlateColor::UseSubduedForeground();
		case EPullItemStatus::MissingInSource: return FStyleColors::Error;
		}
		return FSlateColor::UseForeground();
	}

	FString FormatSize(int64 Bytes)
	{
		if (Bytes >= 1024 * 1024) { return FString::Printf(TEXT("%.1f MB"), Bytes / (1024.0 * 1024.0)); }
		if (Bytes >= 1024)        { return FString::Printf(TEXT("%.0f KB"), Bytes / 1024.0); }
		return FString::Printf(TEXT("%lld B"), Bytes);
	}
}

void SPullConfirmDialog::Construct(const FArguments& InArgs)
{
	Plan = InArgs._Plan;
	ParentWindow = InArgs._ParentWindow;

	ListItems.Reserve(Plan->Items.Num());
	for (const FPullItem& Item : Plan->Items)
	{
		ListItems.Add(MakeShared<FPullItem>(Item));
	}

	TSharedRef<SVerticalBox> Summary = SNew(SVerticalBox);

	Summary->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
	[
		SNew(STextBlock)
		.Text(FText::Format(LOCTEXT("SummaryCopy", "{0} new asset(s) will be copied ({1})"),
			FText::AsNumber(Plan->NumToCopy), FText::FromString(FormatSize(Plan->TotalCopyBytes))))
		.ColorAndOpacity(FStyleColors::AccentGreen)
	];
	if (Plan->NumToUpdate > 0)
	{
		Summary->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("SummaryUpdate", "{0} existing asset(s) will be OVERWRITTEN with the source version ({1}) — the old files are backed up to Saved/AssetPullerBackups"),
				FText::AsNumber(Plan->NumToUpdate), FText::FromString(FormatSize(Plan->TotalUpdateBytes))))
			.ColorAndOpacity(FStyleColors::Warning)
			.AutoWrapText(true)
		];
	}
	if (Plan->NumExisting > 0)
	{
		Summary->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("SummarySkip", "{0} already exist in this project and will be skipped"),
				FText::AsNumber(Plan->NumExisting)))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
	}
	if (Plan->NumMissing > 0)
	{
		Summary->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("SummaryMissing", "{0} referenced asset(s) are MISSING in the source library — those references may already be broken in the source project"),
				FText::AsNumber(Plan->NumMissing)))
			.ColorAndOpacity(FStyleColors::Error)
			.AutoWrapText(true)
		];
	}
	if (Plan->NumMaps > 0)
	{
		Summary->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("SummaryMaps", "Warning: includes {0} map(s) — maps can pull in a very large number of dependencies"),
				FText::AsNumber(Plan->NumMaps)))
			.ColorAndOpacity(FStyleColors::Warning)
			.AutoWrapText(true)
		];
	}
	if (Plan->ExternalRefs.Num() > 0)
	{
		Summary->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("SummaryExternal", "Also references built-in content every project already has ({0}) — not copied"),
				FText::FromString(FString::Join(Plan->ExternalRefs, TEXT(", ")))))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.AutoWrapText(true)
		];
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(12)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight()
			[
				Summary
			]

			+ SVerticalBox::Slot().FillHeight(1.f).Padding(0, 6)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
				.Padding(4)
				[
					SNew(SListView<TSharedPtr<FPullItem>>)
					.ListItemsSource(&ListItems)
					.OnGenerateRow(this, &SPullConfirmDialog::OnGenerateRow)
					.SelectionMode(ESelectionMode::None)
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNullWidget::NullWidget
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
					.IsEnabled(Plan->NumToCopy + Plan->NumToUpdate > 0)
					.Text(Plan->NumToUpdate > 0
						? FText::Format(LOCTEXT("ImportUpdateButton", "Import {0}, Overwrite {1}"),
							FText::AsNumber(Plan->NumToCopy), FText::AsNumber(Plan->NumToUpdate))
						: FText::Format(LOCTEXT("ImportButton", "Import {0} Asset(s)"), FText::AsNumber(Plan->NumToCopy)))
					.OnClicked(this, &SPullConfirmDialog::OnImportClicked)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("CancelButton", "Cancel"))
					.OnClicked(this, &SPullConfirmDialog::OnCancelClicked)
				]
			]
		]
	];
}

TSharedRef<ITableRow> SPullConfirmDialog::OnGenerateRow(TSharedPtr<FPullItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	FString Label = Item->PackageName;
	if (Item->bSoftOnly) { Label += TEXT("  (soft ref)"); }
	if (Item->bIsRequested) { Label += TEXT("  [requested]"); }

	return SNew(STableRow<TSharedPtr<FPullItem>>, OwnerTable)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 2)
		[
			SNew(SBox).WidthOverride(120)
			[
				SNew(STextBlock)
				.Text(StatusText(*Item))
				.ColorAndOpacity(StatusColor(*Item))
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(4, 2)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Label))
			.ToolTipText(FText::FromString(Item->SourceFile))
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 2)
		[
			SNew(STextBlock)
			.Text(FText::FromString(
				(Item->Status == EPullItemStatus::CopyNew || Item->Status == EPullItemStatus::UpdateExisting)
					? FormatSize(Item->FileSize) : FString()))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
	];
}

FReply SPullConfirmDialog::OnImportClicked()
{
	bConfirmed = true;
	if (ParentWindow.IsValid()) { ParentWindow->RequestDestroyWindow(); }
	return FReply::Handled();
}

FReply SPullConfirmDialog::OnCancelClicked()
{
	bConfirmed = false;
	if (ParentWindow.IsValid()) { ParentWindow->RequestDestroyWindow(); }
	return FReply::Handled();
}

bool SPullConfirmDialog::ShowModal(const FPullPlan& Plan, const TSharedPtr<SWidget>& ParentWidget)
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("ConfirmTitle", "Confirm Import"))
		.ClientSize(FVector2D(760, 540))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	TSharedRef<SPullConfirmDialog> Dialog = SNew(SPullConfirmDialog)
		.Plan(&Plan)
		.ParentWindow(Window);

	Window->SetContent(Dialog);
	FSlateApplication::Get().AddModalWindow(Window, ParentWidget);
	return Dialog->bConfirmed;
}

#undef LOCTEXT_NAMESPACE
