// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAddNewRestrictedMessageTagWidget.h"
#include "DetailLayoutBuilder.h"
#include "MessageTagsSettings.h"
#include "MessageTagsEditorModule.h"
#include "MessageTagsModule.h"
#include "SMessageTagWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SGridPanel.h"


#define LOCTEXT_NAMESPACE "AddNewRestrictedMessageTagWidget"

SAddNewRestrictedMessageTagWidget::~SAddNewRestrictedMessageTagWidget()
{
	if (!GExitPurge)
	{
		IMessageTagsModule::OnTagSettingsChanged.RemoveAll(this);
	}
}

void SAddNewRestrictedMessageTagWidget::Construct(const FArguments& InArgs)
{
	FText HintText = LOCTEXT("NewTagNameHint", "X.Y.Z");
	DefaultNewName = InArgs._NewRestrictedTagName;
	if (DefaultNewName.IsEmpty() == false)
	{
		HintText = FText::FromString(DefaultNewName);
	}


	bAddingNewRestrictedTag = false;
	bShouldGetKeyboardFocus = false;

	OnRestrictedMessageTagAdded = InArgs._OnRestrictedMessageTagAdded;
	PopulateTagSources();

	IMessageTagsModule::OnTagSettingsChanged.AddRaw(this, &SAddNewRestrictedMessageTagWidget::PopulateTagSources);

	ChildSlot
	[
		SNew(SVerticalBox)

		// Restricted Tag Name
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
				.Text(LOCTEXT("NewTagName", "Name:"))
			]

			+ SHorizontalBox::Slot()
			.Padding(2.0f, 2.0f)
			.FillWidth(1.0f)
			.HAlign(HAlign_Right)
			[
				SAssignNew(TagNameTextBox, SEditableTextBox)
				.MinDesiredWidth(240.0f)
				.HintText(HintText)
				.OnTextCommitted(this, &SAddNewRestrictedMessageTagWidget::OnCommitNewTagName)
			]
		]

		// Tag Comment
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
				.Text(LOCTEXT("TagComment", "Comment:"))
			]

			+ SHorizontalBox::Slot()
			.Padding(2.0f, 2.0f)
			.FillWidth(1.0f)
			.HAlign(HAlign_Right)
			[
				SAssignNew(TagCommentTextBox, SEditableTextBox)
				.MinDesiredWidth(240.0f)
				.HintText(LOCTEXT("TagCommentHint", "Comment"))
				.OnTextCommitted(this, &SAddNewRestrictedMessageTagWidget::OnCommitNewTagName)
			]
		]

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
				.Text(LOCTEXT("AllowNonRestrictedChildren", "Allow non-restricted children:"))
			]

			+ SHorizontalBox::Slot()
			.Padding(2.0f, 2.0f)
			.FillWidth(1.0f)
			.HAlign(HAlign_Right)
			[
				SAssignNew(AllowNonRestrictedChildrenCheckBox, SCheckBox)
			]
			]

		// Tag Location
		+ SVerticalBox::Slot()
		.AutoHeight()
		.VAlign(VAlign_Top)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(2.0f, 6.0f)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CreateTagSource", "Source:"))
			]

			+ SHorizontalBox::Slot()
			.Padding(2.0f, 2.0f)
			.FillWidth(1.0f)
			.HAlign(HAlign_Right)
			[
				SAssignNew(TagSourcesComboBox, SComboBox<TSharedPtr<FName> >)
				.OptionsSource(&RestrictedTagSources)
				.OnGenerateWidget(this, &SAddNewRestrictedMessageTagWidget::OnGenerateTagSourcesComboBox)
				.ContentPadding(2.0f)
				.Content()
				[
					SNew(STextBlock)
					.Text(this, &SAddNewRestrictedMessageTagWidget::CreateTagSourcesComboBoxContent)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			]
		]

		// Add Tag Button
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
				.Text(LOCTEXT("AddNew", "Add New Tag"))
				.OnClicked(this, &SAddNewRestrictedMessageTagWidget::OnAddNewTagButtonPressed)
			]
		]
	];

	Reset();
}

void SAddNewRestrictedMessageTagWidget::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	if (bShouldGetKeyboardFocus)
	{
		bShouldGetKeyboardFocus = false;
		FSlateApplication::Get().SetKeyboardFocus(TagNameTextBox.ToSharedRef(), EFocusCause::SetDirectly);
	}
}

void SAddNewRestrictedMessageTagWidget::PopulateTagSources()
{
	UMessageTagsManager& Manager = UMessageTagsManager::Get();
	RestrictedTagSources.Empty();

	TArray<const FMessageTagSource*> Sources;
	Manager.GetRestrictedTagSources(Sources);

	// Used to make sure we have a non-empty list of restricted tag sources. Not an actual source.
	FName PlaceholderSource = NAME_None;

	// Add the placeholder source if no other sources exist
	if (Sources.Num() == 0)
	{
		RestrictedTagSources.Add(MakeShareable(new FName(PlaceholderSource)));
	}

	for (const FMessageTagSource* Source : Sources)
	{
		if (Source != nullptr && Source->SourceName != PlaceholderSource)
		{
			RestrictedTagSources.Add(MakeShareable(new FName(Source->SourceName)));
		}
	}
}

void SAddNewRestrictedMessageTagWidget::Reset(FName TagSource)
{
	SetTagName();
	SelectTagSource(TagSource);
	SetAllowNonRestrictedChildren();
	TagCommentTextBox->SetText(FText());
}

void SAddNewRestrictedMessageTagWidget::SetTagName(const FText& InName)
{
	TagNameTextBox->SetText(InName.IsEmpty() ? FText::FromString(DefaultNewName) : InName);
}

void SAddNewRestrictedMessageTagWidget::SelectTagSource(const FName& InSource)
{
	// Attempt to find the location in our sources, otherwise just use the first one
	int32 SourceIndex = 0;

	if (!InSource.IsNone())
	{
		for (int32 Index = 0; Index < RestrictedTagSources.Num(); ++Index)
		{
			TSharedPtr<FName> Source = RestrictedTagSources[Index];

			if (Source.IsValid() && *Source.Get() == InSource)
			{
				SourceIndex = Index;
				break;
			}
		}
	}

	TagSourcesComboBox->SetSelectedItem(RestrictedTagSources[SourceIndex]);
}

void SAddNewRestrictedMessageTagWidget::SetAllowNonRestrictedChildren(bool bInAllowNonRestrictedChildren)
{
	AllowNonRestrictedChildrenCheckBox->SetIsChecked(bInAllowNonRestrictedChildren ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
}

void SAddNewRestrictedMessageTagWidget::OnCommitNewTagName(const FText& InText, ETextCommit::Type InCommitType)
{
	if (InCommitType == ETextCommit::OnEnter)
	{
		ValidateNewRestrictedTag();
	}
}

FReply SAddNewRestrictedMessageTagWidget::OnAddNewTagButtonPressed()
{
	ValidateNewRestrictedTag();
	return FReply::Handled();
}

void SAddNewRestrictedMessageTagWidget::AddSubtagFromParent(const FString& ParentTagName, const FName& ParentTagSource, bool bAllowNonRestrictedChildren)
{
	FText SubtagBaseName = !ParentTagName.IsEmpty() ? FText::Format(FText::FromString(TEXT("{0}.")), FText::FromString(ParentTagName)) : FText();

	SetTagName(SubtagBaseName);
	SelectTagSource(ParentTagSource);
	SetAllowNonRestrictedChildren(bAllowNonRestrictedChildren);

	bShouldGetKeyboardFocus = true;
}

void SAddNewRestrictedMessageTagWidget::AddDuplicate(const FString& ParentTagName, const FName& ParentTagSource, bool bAllowNonRestrictedChildren)
{
	SetTagName(FText::FromString(ParentTagName));
	SelectTagSource(ParentTagSource);
	SetAllowNonRestrictedChildren(bAllowNonRestrictedChildren);

	bShouldGetKeyboardFocus = true;
}

void SAddNewRestrictedMessageTagWidget::ValidateNewRestrictedTag()
{
	UMessageTagsManager& Manager = UMessageTagsManager::Get();

	FString TagName = TagNameTextBox->GetText().ToString();
	FString TagComment = TagCommentTextBox->GetText().ToString();
	const FName TagSource = *TagSourcesComboBox->GetSelectedItem().Get();

	if (TagSource == NAME_None)
	{
		FNotificationInfo Info(LOCTEXT("NoRestrictedSource", "You must specify a source file for restricted gameplay tags."));
		Info.ExpireDuration = 10.f;
		Info.bUseSuccessFailIcons = true;
		Info.Image = FGMPStyle::GetBrush(TEXT("MessageLog.Error"));

		AddRestrictedMessageTagDialog = FSlateNotificationManager::Get().AddNotification(Info);

		return;
	}

	TArray<FString> TagSourceOwners;
	Manager.GetOwnersForTagSource(TagSource.ToString(), TagSourceOwners);

	bool bHasOwner = false;
	for (const FString& Owner : TagSourceOwners)
	{
		if (!Owner.IsEmpty())
		{
			bHasOwner = true;
			break;
		}
	}

	if (bHasOwner)
	{
		// check if we're one of the owners; if we are then we don't need to pop up the permission dialog
		bool bRequiresPermission = true;
		const FString& UserName = FPlatformProcess::UserName();
		for (const FString& Owner : TagSourceOwners)
		{
			if (Owner.Equals(UserName))
			{
				CreateNewRestrictedMessageTag();
				bRequiresPermission = false;
			}
		}

		if (bRequiresPermission)
		{
			FString StringToDisplay = TEXT("Do you have permission from ");
			StringToDisplay.Append(TagSourceOwners[0]);
			for (int Idx = 1; Idx < TagSourceOwners.Num(); ++Idx)
			{
				StringToDisplay.Append(TEXT(" or "));
				StringToDisplay.Append(TagSourceOwners[Idx]);
			}
			StringToDisplay.Append(TEXT(" to modify "));
			StringToDisplay.Append(TagSource.ToString());
			StringToDisplay.Append(TEXT("?"));

			FNotificationInfo Info(FText::FromString(StringToDisplay));
			Info.ExpireDuration = 10.f;
			Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("RestrictedTagPopupButtonAccept", "Yes"), FText(), FSimpleDelegate::CreateSP(this, &SAddNewRestrictedMessageTagWidget::CreateNewRestrictedMessageTag), SNotificationItem::CS_None));
			Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("RestrictedTagPopupButtonReject", "No"), FText(), FSimpleDelegate::CreateSP(this, &SAddNewRestrictedMessageTagWidget::CancelNewTag), SNotificationItem::CS_None));

			AddRestrictedMessageTagDialog = FSlateNotificationManager::Get().AddNotification(Info);
		}
	}
	else
	{
		CreateNewRestrictedMessageTag();
	}
}

void SAddNewRestrictedMessageTagWidget::CreateNewRestrictedMessageTag()
{
	if (AddRestrictedMessageTagDialog.IsValid())
	{
		AddRestrictedMessageTagDialog->SetVisibility(EVisibility::Collapsed);
	}

	const UMessageTagsManager& Manager = UMessageTagsManager::Get();

	// Only support adding tags via ini file
	if (Manager.ShouldImportTagsFromINI() == false)
	{
		return;
	}

	FString TagName = TagNameTextBox->GetText().ToString();
	FString TagComment = TagCommentTextBox->GetText().ToString();
	bool bAllowNonRestrictedChildren = AllowNonRestrictedChildrenCheckBox->IsChecked();
	FName TagSource = *TagSourcesComboBox->GetSelectedItem().Get();

	if (TagName.IsEmpty())
	{
		return;
	}

	// set bIsAddingNewTag, this guards against the window closing when it loses focus due to source control checking out a file
	TGuardValue<bool>	Guard(bAddingNewRestrictedTag, true);

	IMessageTagsEditorModule::Get().AddNewMessageTagToINI(TagName, TagComment, TagSource, true, bAllowNonRestrictedChildren);

	OnRestrictedMessageTagAdded.ExecuteIfBound(TagName, TagComment, TagSource);

	Reset(TagSource);
}

void SAddNewRestrictedMessageTagWidget::CancelNewTag()
{
	if (AddRestrictedMessageTagDialog.IsValid())
	{
		AddRestrictedMessageTagDialog->SetVisibility(EVisibility::Collapsed);
	}
}

TSharedRef<SWidget> SAddNewRestrictedMessageTagWidget::OnGenerateTagSourcesComboBox(TSharedPtr<FName> InItem)
{
	return SNew(STextBlock)
		.Text(FText::FromName(*InItem.Get()));
}

FText SAddNewRestrictedMessageTagWidget::CreateTagSourcesComboBoxContent() const
{
	const bool bHasSelectedItem = TagSourcesComboBox.IsValid() && TagSourcesComboBox->GetSelectedItem().IsValid();

	return bHasSelectedItem ? FText::FromName(*TagSourcesComboBox->GetSelectedItem().Get()) : LOCTEXT("NewTagLocationNotSelected", "Not selected");
}

#undef LOCTEXT_NAMESPACE
