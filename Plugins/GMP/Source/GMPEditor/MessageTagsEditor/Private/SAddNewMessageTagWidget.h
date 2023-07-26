// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MessageTagsManager.h"
#include "Input/Reply.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboBox.h"

struct FEdGraphPinType;
struct FMessageParameterDetail;

/** Widget allowing the user to create new message tags */
class SAddNewMessageTagWidget : public SCompoundWidget
{
public:

	enum class EResetType : uint8
	{
		ResetAll,
		DoNotResetSource
	};

	DECLARE_DELEGATE_ThreeParams(FOnMessageTagAdded, const FString& /*TagName*/, const FString& /*TagComment*/, const FName& /*TagSource*/);

	DECLARE_DELEGATE_RetVal_TwoParams(bool, FIsValidTag, const FString& /*TagName*/, FText* /*OutError*/)

	SLATE_BEGIN_ARGS(SAddNewMessageTagWidget)
		: _NewTagName(TEXT(""))
	{}
		SLATE_EVENT(FOnMessageTagAdded, OnMessageTagAdded)	// Callback for when a new tag is added	
		SLATE_EVENT(FIsValidTag, IsValidTag)
		SLATE_ARGUMENT(FString, NewTagName) // String that will initially populate the New Tag Name field
	SLATE_END_ARGS();

	MESSAGETAGSEDITOR_API virtual ~SAddNewMessageTagWidget();

	MESSAGETAGSEDITOR_API virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	MESSAGETAGSEDITOR_API void Construct(const FArguments& InArgs);

	/** Returns true if we're currently attempting to add a new message tag to an INI file */
	bool IsAddingNewTag() const
	{
		return bAddingNewTag;
	}

	/** Begins the process of adding a subtag to a parent tag */
	void AddSubtagFromParent(const FString& ParentTagName, const FName& ParentTagSource);

	/** Resets all input fields */
	void Reset(EResetType ResetType);

private:

	/** Sets the name of the tag. Uses the default if the name is not specified */
	void SetTagName(const FText& InName = FText());

	/** Selects tag file location. Uses the default if the location is not specified */
	void SelectTagSource(const FName& InSource = FName());

	/** Creates a list of all INIs that message tags can be added to */
	void PopulateTagSources();

	/** Callback for when Enter is pressed when modifying a tag's name or comment */
	void OnCommitNewTagName(const FText& InText, ETextCommit::Type InCommitType);

	/** Callback for when the Add New Tag button is pressed */
	FReply OnAddNewTagButtonPressed();

	/** Creates a new message tag and adds it to the INI files based on the widget's stored parameters */
	void CreateNewMessageTag();

	/** Populates the widget's combo box with all potential places where a message tag can be stored */
	TSharedRef<SWidget> OnGenerateTagSourcesComboBox(TSharedPtr<FName> InItem);

	/** Creates the text displayed by the combo box when an option is selected */
	FText CreateTagSourcesComboBoxContent() const;

	/** Creates the text displayed by the combo box tooltip when an option is selected */
	FText CreateTagSourcesComboBoxToolTip() const;

	EVisibility OnGetTagSourceFavoritesVisibility() const;
	FReply OnToggleTagSourceFavoriteClicked();
	const FSlateBrush* OnGetTagSourceFavoriteImage() const;

private:

	/** All potential INI files where a message tag can be stored */
	TArray<TSharedPtr<FName> > TagSources;

	/** The name of the next message tag to create */
	TSharedPtr<SEditableTextBox> TagNameTextBox;

	/** The comment to asign to the next message tag to create*/
	TSharedPtr<SEditableTextBox> TagCommentTextBox;

	/** The INI file where the next message tag will be creatd */
	TSharedPtr<SComboBox<TSharedPtr<FName> > > TagSourcesComboBox;

	/** Callback for when a new message tag has been added to the INI files */
	FOnMessageTagAdded OnMessageTagAdded;

	/** Callback to see if the gameplay tag is valid. This should be used for any specialized rules that are not covered by IsValidMessageTagString */
	FIsValidTag IsValidTag;

	bool bAddingNewTag;

	/** Tracks if this widget should get keyboard focus */
	bool bShouldGetKeyboardFocus;

	FString DefaultNewName;

	TWeakPtr<class SPinTypeSelector> PinTypeSelector;
	//////////////////////////////////////////////////////////////////////////

	TArray<TSharedPtr<FMessageParameterDetail>> ParameterTypes;
	TWeakPtr<SListView<TSharedPtr<FMessageParameterDetail>>> ListViewParameters;
	FReply OnAddNewParameterTypesButtonPressed();

	TArray<TSharedPtr<FMessageParameterDetail>> ResponseTypes;
	TWeakPtr<SListView<TSharedPtr<FMessageParameterDetail>>> ListViewResponseTypes;
	FReply OnAddNewResponeTypesButtonPressed();

	TSharedRef<class ITableRow> OnGenerateParameterRow(TSharedPtr<FMessageParameterDetail> InItem, const TSharedRef<STableViewBase>& OwnerTable, bool bIsResponse, TWeakPtr<SListView<TSharedPtr<FMessageParameterDetail>>> WeakListView);
	const FEdGraphPinType& GetPinInfo(const TSharedPtr<FMessageParameterDetail>& InItem);
	void OnRemoveClicked(const TSharedPtr<FMessageParameterDetail>& InItem, bool bIsResponse, TWeakPtr<SListView<TSharedPtr<FMessageParameterDetail>>> WeakListView);
};
