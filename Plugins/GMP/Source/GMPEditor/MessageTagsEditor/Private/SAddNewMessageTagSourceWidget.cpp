// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAddNewMessageTagSourceWidget.h"
#include "DetailLayoutBuilder.h"
#include "MessageTagsSettings.h"
#include "MessageTagsEditorModule.h"
#include "MessageTagsModule.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "AddNewMessageTagSourceWidget"

void SAddNewMessageTagSourceWidget::Construct(const FArguments& InArgs)
{
	FText HintText = LOCTEXT("NewSourceNameHint", "SourceName.ini");
	DefaultNewName = InArgs._NewSourceName;
	if (DefaultNewName.IsEmpty() == false)
	{
		HintText = FText::FromString(DefaultNewName);
	}

	bShouldGetKeyboardFocus = false;

	OnMessageTagSourceAdded = InArgs._OnMessageTagSourceAdded;

	ChildSlot
	[
		SNew(SVerticalBox)

		// Tag Name
		+ SVerticalBox::Slot()
		.AutoHeight()
		.VAlign(VAlign_Top)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(2.0f, 4.0f)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NewSourceName", "Name:"))
			]

			+ SHorizontalBox::Slot()
			.Padding(2.0f, 2.0f)
			.FillWidth(1.0f)
			.HAlign(HAlign_Right)
			[
				SAssignNew(SourceNameTextBox, SEditableTextBox)
				.MinDesiredWidth(240.0f)
				.HintText(HintText)
			]
		]

		// Add Source Button
		+SVerticalBox::Slot()
		.AutoHeight()
		.VAlign(VAlign_Top)
		.HAlign(HAlign_Center)
		.Padding(8.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("AddNew", "Add New Source"))
				.OnClicked(this, &SAddNewMessageTagSourceWidget::OnAddNewSourceButtonPressed)
			]
		]
	];

	Reset();
}

void SAddNewMessageTagSourceWidget::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	if (bShouldGetKeyboardFocus)
	{
		bShouldGetKeyboardFocus = false;
		FSlateApplication::Get().SetKeyboardFocus(SourceNameTextBox.ToSharedRef(), EFocusCause::SetDirectly);
	}
}

void SAddNewMessageTagSourceWidget::Reset()
{
	SetSourceName();
}

void SAddNewMessageTagSourceWidget::SetSourceName(const FText& InName)
{
	SourceNameTextBox->SetText(InName.IsEmpty() ? FText::FromString(DefaultNewName) : InName);
}

FReply SAddNewMessageTagSourceWidget::OnAddNewSourceButtonPressed()
{
	UMessageTagsManager& Manager = UMessageTagsManager::Get();
		
	if (!SourceNameTextBox->GetText().EqualTo(FText::FromString(DefaultNewName)))
	{
		Manager.FindOrAddTagSource(*SourceNameTextBox->GetText().ToString(), EMessageTagSourceType::TagList);
	}

	IMessageTagsModule::OnTagSettingsChanged.Broadcast();

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
