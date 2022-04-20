// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "SGraphPin.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"
#include "SMessageTagWidget.h"

class SComboButton;

class MESSAGETAGSEDITOR_API SMessageTagGraphPin : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SMessageTagGraphPin): _bRawName(false),_bRefresh(false) {}
		SLATE_ARGUMENT(bool, bRawName)
		SLATE_ARGUMENT(bool, bRefresh)
		SLATE_ARGUMENT(TSharedPtr<FMessageTagContainer>, TagContainer)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);

	//~ Begin SGraphPin Interface
	virtual TSharedRef<SWidget> GetLabelWidget(const FName& InPinLabelStyle) override;
	virtual TSharedRef<SWidget> GetDefaultValueWidget() override;
	//~ End SGraphPin Interface

private:

	/** Refreshes the list of tags displayed on the node. */
	void RefreshTagList(bool bRefresh = false);
	FReply FindInAllBlueprints();

	/** Parses the Data from the pin to fill in the names of the array. */
	void ParseDefaultValueData();

	/** Callback function to create content for the combo button. */
	TSharedRef<SWidget> GetListContent();
	void OnMenuOpenChanged(bool bOpened);
	/** 
	 * Creates SelectedTags List.
	 * @return widget that contains the read only tag names for displaying on the node.
	 */
	TSharedRef<SWidget> SelectedTags();

	/** 
	 * Callback for populating rows of the SelectedTags List View.
	 * @return widget that contains the name of a tag.
	 */
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable);

private:

	// Combo Button for the drop down list.
	TSharedPtr<SComboButton> ComboButton;

	// Tag Container used for the MessageTagWidget.
	TSharedPtr<FMessageTagContainer> TagContainer;

	// Datum uses for the MessageTagWidget.
	TArray<SMessageTagWidget::FEditableMessageTagContainerDatum> EditableContainers;

	// Array of names for the read only display of tag names on the node.
	TArray< TSharedPtr<FString> > TagNames;

	// The List View used to display the read only tag names on the node.
	TSharedPtr<SListView<TSharedPtr<FString>>> TagListView;

	TSharedPtr<SMessageTagWidget> TagWidget;

	FString FilterString;
	bool bUseRawName;
	bool bCloseWidget;
	FName LastName;
};
