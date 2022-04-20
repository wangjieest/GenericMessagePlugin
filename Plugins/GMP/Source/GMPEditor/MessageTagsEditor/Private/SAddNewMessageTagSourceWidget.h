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

/** Widget allowing the user to create new message tags */
class SAddNewMessageTagSourceWidget : public SCompoundWidget
{
public:

	DECLARE_DELEGATE_OneParam( FOnMessageTagSourceAdded, const FString& /*SourceName*/);

	SLATE_BEGIN_ARGS(SAddNewMessageTagSourceWidget)
		: _NewSourceName(TEXT(""))
		{}
		SLATE_EVENT(FOnMessageTagSourceAdded, OnMessageTagSourceAdded )	// Callback for when a new source is added	
		SLATE_ARGUMENT( FString, NewSourceName ) // String that will initially populate the New Source Name field
	SLATE_END_ARGS();

	virtual void Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime ) override;

	void Construct( const FArguments& InArgs);

	/** Resets all input fields */
	void Reset();

private:

	/** Sets the name of the source. Uses the default if the name is not specified */
	void SetSourceName(const FText& InName = FText());

	/** Callback for when the Add New Tag button is pressed */
	FReply OnAddNewSourceButtonPressed();

private:

	/** The name of the next message tag source to create */
	TSharedPtr<SEditableTextBox> SourceNameTextBox;

	/** Callback for when a new message tag has been added to the INI files */
	FOnMessageTagSourceAdded OnMessageTagSourceAdded;

	/** Tracks if this widget should get keyboard focus */
	bool bShouldGetKeyboardFocus;

	FString DefaultNewName;
};
