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

class SEditableTextBox;
template <typename OptionType> class SComboBox;

/** Widget allowing the user to create new message tags */
class SAddNewMessageTagSourceWidget : public SCompoundWidget
{
public:

	DECLARE_DELEGATE_OneParam( FOnMessageTagSourceAdded, const FString& /*SourceName*/);

	SLATE_BEGIN_ARGS(SAddNewMessageTagSourceWidget)
		: _NewSourceName(TEXT(""))
		, _Padding(FMargin(15))
		{}
		SLATE_EVENT(FOnMessageTagSourceAdded, OnMessageTagSourceAdded )	// Callback for when a new source is added	
		SLATE_ARGUMENT( FString, NewSourceName ) // String that will initially populate the New Source Name field
		SLATE_ARGUMENT(FMargin, Padding)
	SLATE_END_ARGS();

	virtual void Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime ) override;

	void Construct( const FArguments& InArgs);

	/** Resets all input fields */
	void Reset();

private:

	/** Sets the name of the source. Uses the default if the name is not specified */
	void SetSourceName(const FText& InName = FText());

	/** Creates a list of all root directories for tag sources */
	void PopulateTagRoots();

	/** Populates the widget's combo box with all potential places where a tag source can be stored */
	TSharedRef<SWidget> OnGenerateTagRootsComboBox(TSharedPtr<FString> InItem);

	/** Gets friendly version of a root path */
	FText GetFriendlyPath(TSharedPtr<FString> InItem) const;

	/** Creates the text displayed by the combo box when an option is selected */
	FText CreateTagRootsComboBoxContent() const;

	/** Callback for when the Add New Tag button is pressed */
	FReply OnAddNewSourceButtonPressed();

	/** The name of the next message tag source to create */
	TSharedPtr<SEditableTextBox> SourceNameTextBox;

	/** Callback for when a new message tag has been added to the INI files */
	FOnMessageTagSourceAdded OnMessageTagSourceAdded;

	/** All potential INI files where a tag source can be stored */
	TArray<TSharedPtr<FString> > TagRoots;

	/** The directory where the next tag source will be created */
	TSharedPtr<SComboBox<TSharedPtr<FString> > > TagRootsComboBox;

	/** Tracks if this widget should get keyboard focus */
	bool bShouldGetKeyboardFocus;

	FString DefaultNewName;
};
