// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MessageTagsManager.h"
#include "Input/Reply.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Views/SListView.h"
#include "EdGraph/EdGraphPin.h"
#include "Misc/EngineVersionComparison.h"

class SEditableTextBox;
struct FGameplayTagNode;
#if UE_VERSION_NEWER_THAN(5, 2, 0)
namespace ETextCommit { enum Type : int; }
#elif UE_VERSION_NEWER_THAN(5, 0, 0)
namespace ETextCommit { enum Type; }
#endif

class SRenameMessageTagDialog : public SCompoundWidget
{
public:

	DECLARE_DELEGATE_TwoParams( FOnMessageTagRenamed, FString/* OldName */, FString/* NewName */);

	SLATE_BEGIN_ARGS( SRenameMessageTagDialog )
		: _MessageTagNode()
		, _Padding(FMargin(15))
		,_bAllowFullEdit(true)
		{}
		SLATE_ARGUMENT( TSharedPtr<FMessageTagNode>, MessageTagNode )		// The message tag we want to rename
		SLATE_EVENT(FOnMessageTagRenamed, OnMessageTagRenamed)	// Called when the tag is renamed
		SLATE_ARGUMENT(FMargin, Padding)
		SLATE_ARGUMENT(bool, bAllowFullEdit)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:

	/** Checks if we're in a valid state to rename the tag */
	bool IsRenameEnabled() const;

	/** Renames the tag based on dialog parameters */
	FReply OnRenameClicked();

	/** Callback for when Cancel is clicked */
	FReply OnCancelClicked();

	/** Renames the tag and attempts to close the active window */
	void RenameAndClose();

	/** Attempts to rename the tag if enter is pressed while editing the tag name */
	void OnRenameTextCommitted(const FText& InText, ETextCommit::Type InCommitType);

	/** Closes the window that contains this widget */
	void CloseContainingWindow();

private:

	TSharedPtr<FMessageTagNode> MessageTagNode;

	TSharedPtr<SEditableTextBox> NewTagNameTextBox;

	FOnMessageTagRenamed OnMessageTagRenamed;

	bool bAllowFullEdit;
	//////////////////////////////////////////////////////////////////////////
	/** The comment to asign to the next message tag to create*/
	TSharedPtr<SEditableTextBox> TagCommentTextBox;

	TWeakPtr<class SPinTypeSelector> PinTypeSelector;

	struct FMessageParameterDetail : FMessageParameter
	{
		FEdGraphPinType PinType;
	};

	TArray<TSharedPtr<FMessageParameterDetail>> ParameterTypes;
	TWeakPtr<SListView<TSharedPtr<FMessageParameterDetail>>> ListViewParameters;
	FReply OnAddNewParameterButtonPressed();

	TArray<TSharedPtr<FMessageParameterDetail>> ResponseTypes;
	TWeakPtr<SListView<TSharedPtr<FMessageParameterDetail>>> ListViewResponseTypes;
	FReply OnAddNewResponeTypeButtonPressed();

	TSharedRef<class ITableRow> OnGenerateParameterRow(TSharedPtr<FMessageParameterDetail> InItem, const TSharedRef<STableViewBase>& OwnerTable, TWeakPtr<SListView<TSharedPtr<FMessageParameterDetail>>> WeakListView);

	FEdGraphPinType GetPinInfo(const TSharedPtr<FMessageParameterDetail>& InItem);
	void OnRemoveClicked(const TSharedPtr<FMessageParameterDetail>& InItem, TWeakPtr<SListView<TSharedPtr<FMessageParameterDetail>>> WeakListView);
};
