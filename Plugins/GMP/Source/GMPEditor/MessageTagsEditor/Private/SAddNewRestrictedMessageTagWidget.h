// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Misc/EngineVersionComparison.h"

class SCheckBox;
class SEditableTextBox;
class SNotificationItem;
template <typename OptionType> class SComboBox;
#if UE_VERSION_NEWER_THAN(5, 2, 0)
namespace ETextCommit { enum Type : int; }
#if UE_VERSION_NEWER_THAN(5, 0, 0)
namespace ETextCommit { enum Type; }
#endif

/** Widget allowing the user to create new restricted message tags */
class SAddNewRestrictedMessageTagWidget : public SCompoundWidget
{
public:

	DECLARE_DELEGATE_ThreeParams(FOnRestrictedMessageTagAdded, const FString& /*TagName*/, const FString& /*TagComment*/, const FName& /*TagSource*/);

	SLATE_BEGIN_ARGS(SAddNewRestrictedMessageTagWidget)
		: _NewRestrictedTagName(TEXT(""))
		, _Padding(FMargin(15))
		{}
		SLATE_EVENT( FOnRestrictedMessageTagAdded, OnRestrictedMessageTagAdded )	// Callback for when a new tag is added	
		SLATE_ARGUMENT( FString, NewRestrictedTagName ) // String that will initially populate the New Tag Name field
		SLATE_ARGUMENT(FMargin, Padding)
	SLATE_END_ARGS();

	virtual ~SAddNewRestrictedMessageTagWidget() override;

	virtual void Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime ) override;

	void Construct( const FArguments& InArgs);

	/** Returns true if we're currently attempting to add a new restricted message tag to an INI file */
	bool IsAddingNewRestrictedTag() const
	{
		return bAddingNewRestrictedTag;
	}

	/** Begins the process of adding a subtag to a parent tag */
	void AddSubtagFromParent(const FString& ParentTagName, const FName& ParentTagSource, bool bAllowNonRestrictedChildren);

	/** Begins the process of adding a duplicate of existing tag */
	void AddDuplicate(const FString& ParentTagName, const FName& ParentTagSource, bool bAllowNonRestrictedChildren);

	/** Resets all input fields */
	void Reset(FName TagSource = NAME_None);

private:

	/** Sets the name of the tag. Uses the default if the name is not specified */
	void SetTagName(const FText& InName = FText());

	/** Selects tag file location. Uses the default if the location is not specified */
	void SelectTagSource(const FName& InSource = FName());

	void SetAllowNonRestrictedChildren(bool bInAllowNonRestrictedChildren = false);

	/** Creates a list of all INIs that message tags can be added to */
	void PopulateTagSources();

	/** Callback for when Enter is pressed when modifying a tag's name or comment */
	void OnCommitNewTagName(const FText& InText, ETextCommit::Type InCommitType);

	/** Callback for when the Add New Tag button is pressed */
	FReply OnAddNewTagButtonPressed();

	void ValidateNewRestrictedTag();

	/** Creates a new restricted message tag and adds it to the INI files based on the widget's stored parameters */
	void CreateNewRestrictedMessageTag();

	void CancelNewTag();

	/** Populates the widget's combo box with all potential places where a message tag can be stored */
	TSharedRef<SWidget> OnGenerateTagSourcesComboBox(TSharedPtr<FName> InItem);

	/** Creates the text displayed by the combo box when an option is selected */
	FText CreateTagSourcesComboBoxContent() const;

private:

	/** All potential INI files where a message tag can be stored */
	TArray<TSharedPtr<FName> > RestrictedTagSources;

	/** The name of the next message tag to create */
	TSharedPtr<SEditableTextBox> TagNameTextBox;

	/** The comment to asign to the next message tag to create*/
	TSharedPtr<SEditableTextBox> TagCommentTextBox;

	TSharedPtr<SCheckBox> AllowNonRestrictedChildrenCheckBox;

	/** The INI file where the next restricted message tag will be created */
	TSharedPtr<SComboBox<TSharedPtr<FName> > > TagSourcesComboBox;

	/** Callback for when a new restricted message tag has been added to the INI files */
	FOnRestrictedMessageTagAdded OnRestrictedMessageTagAdded;

	bool bAddingNewRestrictedTag;

	/** Tracks if this widget should get keyboard focus */
	bool bShouldGetKeyboardFocus;

	FString DefaultNewName;

	TSharedPtr<SNotificationItem> AddRestrictedMessageTagDialog;
};
