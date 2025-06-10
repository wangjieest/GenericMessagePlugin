// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRenameMessageTagDialog.h"

#include "DetailLayoutBuilder.h"
#include "EdGraphSchema_K2.h"
#include "Framework/Application/SlateApplication.h"
#include "GMPCore.h"
#include "Layout/WidgetPath.h"
#include "MessageTagsEditorModule.h"
#include "PropertyCustomizationHelpers.h"
#include "SPinTypeSelector.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"

#define LOCTEXT_NAMESPACE "RenameMessageTag"

void SRenameMessageTagDialog::Construct(const FArguments& InArgs)
{
	check(InArgs._MessageTagNode.IsValid());

	MessageTagNode = InArgs._MessageTagNode;
	OnMessageTagRenamed = InArgs._OnMessageTagRenamed;
	bAllowFullEdit = InArgs._bAllowFullEdit;

	// Fill Parameters
	ParameterTypes.Reserve(MessageTagNode->Parameters.Num());
	for (auto& Parameter : MessageTagNode->Parameters)
	{
		auto& Ref = Add_GetRef(ParameterTypes, MakeShared<FMessageParameterDetail>());
		Ref->Name = Parameter.Name;
		Ref->Type = Parameter.Type;
		bool bErrorFree = GMPReflection::PinTypeFromString(Parameter.Type.ToString(), Ref->PinType);
		ensureMsgf(bErrorFree, TEXT("ErrorTypeString : %s"), *Parameter.Type.ToString());
	}

	ResponseTypes.Reserve(MessageTagNode->ResponseTypes.Num());
	for (auto& ResponeType : MessageTagNode->ResponseTypes)
	{
		auto& Ref = Add_GetRef(ResponseTypes, MakeShared<FMessageParameterDetail>());
		Ref->Name = ResponeType.Name;
		Ref->Type = ResponeType.Type;
		bool bErrorFree = GMPReflection::PinTypeFromString(ResponeType.Type.ToString(), Ref->PinType);
		ensureMsgf(bErrorFree, TEXT("ErrorTypeString : %s"), *ResponeType.Type.ToString());
	}

	ChildSlot
	[
		SNew(SBorder)
		.Padding(FMargin(15))
		[
			SNew(SVerticalBox)

			// Current name display
			+ SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Top)
			.Padding(4.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CurrentTag", "Current Tag:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.HAlign(HAlign_Right)
				.Padding(8.0f, 0.0f)
				[
					SNew(STextBlock)
					.MinDesiredWidth(184.0f)
					.Text(FText::FromName(MessageTagNode->GetCompleteTag().GetTagName()))
				]
			]

			// New name controls
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.0f)
			.VAlign(VAlign_Top)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("NewTag", "New Tag:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.HAlign(HAlign_Right)
				.Padding(8.0f, 0.0f)
				[
					SAssignNew(NewTagNameTextBox, SEditableTextBox)
					.IsReadOnly(!bAllowFullEdit)
					.Text(FText::FromName(MessageTagNode->GetCompleteTag().GetTagName()))
					.Padding(4.0f)
					.MinDesiredWidth(180.0f)
					.OnTextCommitted(this, &SRenameMessageTagDialog::OnRenameTextCommitted)
				]
			]
			// Add Tag Parameters
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
					.Text(LOCTEXT("NewParameter", "Parameters"))
				]

				+ SHorizontalBox::Slot()
				.Padding(2.0f, 2.0f)
				.FillWidth(1.0f)
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.Text(LOCTEXT("AddNewParameter", "New Parameter"))
					.OnClicked(this, &SRenameMessageTagDialog::OnAddNewParameterButtonPressed)
					.IsEnabled(bAllowFullEdit)
				]
			]
			// Tag Parameters
			+ SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Top)
			[
				SAssignNew(ListViewParameters, SListView<TSharedPtr<FMessageParameterDetail>>)
				#if !UE_5_05_OR_LATER
				.ItemHeight(24)
				#endif
				.ListItemsSource(&ParameterTypes)  //The Items array is the source of this listview
				.OnGenerateRow(this, &SRenameMessageTagDialog::OnGenerateParameterRow, ListViewParameters)
				.SelectionMode(ESelectionMode::None)
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
					.Text(FText::FromString(MessageTagNode->GetDevComment()))
				]
			]
			// Add Respone Types
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
					.Text(LOCTEXT("NewResponseType", "ResponseTypes"))
				]

				+ SHorizontalBox::Slot()
				.Padding(2.0f, 2.0f)
				.FillWidth(1.0f)
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.Text(LOCTEXT("AddNewResponseType", "New ResponseType"))
					.OnClicked(this, &SRenameMessageTagDialog::OnAddNewResponeTypeButtonPressed)
					.IsEnabled(bAllowFullEdit)
				]
			]
			// ResponseTypes
			+ SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Top)
			[
				SAssignNew(ListViewResponseTypes, SListView<TSharedPtr<FMessageParameterDetail>>)
				#if !UE_5_05_OR_LATER
				.ItemHeight(24)
				#endif
				.ListItemsSource(&ResponseTypes)  //The Items array is the source of this listview
				.OnGenerateRow(this, &SRenameMessageTagDialog::OnGenerateParameterRow, ListViewResponseTypes)
				.SelectionMode(ESelectionMode::None)
			]
			// Dialog controls
			+ SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Top)
			.HAlign(HAlign_Center)
			[
				SNew(SHorizontalBox)

				// Rename
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8.0f, 8.0f)
				[
					SNew(SButton)
					.IsFocusable(false)
					.IsEnabled(this, &SRenameMessageTagDialog::IsRenameEnabled)
					.OnClicked(this, &SRenameMessageTagDialog::OnRenameClicked)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("RenameTagButtonText", "Change"))
					]
				]

				// Cancel
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8.0f, 8.0f)
				[
					SNew(SButton)
					.IsFocusable(false)
					.OnClicked(this, &SRenameMessageTagDialog::OnCancelClicked)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("CancelRenameButtonText", "Cancel"))
					]
				]
			]
		]
	];
}

bool SRenameMessageTagDialog::IsRenameEnabled() const
{
	FString CurrentTagText;

	if (NewTagNameTextBox.IsValid())
	{
		CurrentTagText = NewTagNameTextBox->GetText().ToString();
	}
	if (CurrentTagText.IsEmpty())
		return false;

	bool bChanged = TagCommentTextBox->GetText().ToString() != MessageTagNode->GetDevComment() || !CurrentTagText.Equals(MessageTagNode->GetCompleteTag().GetTagName().ToString());
	if (!bChanged)
	{
		bChanged = ParameterTypes.Num() != MessageTagNode->Parameters.Num();
	}
	if (!bChanged)
	{
		for (auto i = 0; i < ParameterTypes.Num(); ++i)
		{
			if (ParameterTypes[i]->Name != MessageTagNode->Parameters[i].Name || ParameterTypes[i]->Type != MessageTagNode->Parameters[i].Type)
			{
				bChanged = true;
				break;
			}
		}
	}
	if (!bChanged)
	{
		bChanged = ResponseTypes.Num() != MessageTagNode->ResponseTypes.Num();
	}
	if (!bChanged)
	{
		for (auto i = 0; i < ResponseTypes.Num(); ++i)
		{
			if (ResponseTypes[i]->Name != MessageTagNode->ResponseTypes[i].Name || ResponseTypes[i]->Type != MessageTagNode->ResponseTypes[i].Type)
			{
				bChanged = true;
				break;
			}
		}
	}
	return bChanged;
}

void SRenameMessageTagDialog::RenameAndClose()
{
	IMessageTagsEditorModule& Module = IMessageTagsEditorModule::Get();

	FString TagToRename = MessageTagNode->GetCompleteTag().GetTagName().ToString();
	FString NewTagName = NewTagNameTextBox->GetText().ToString();

	MessageTagNode->Parameters.Empty(ParameterTypes.Num());
	for (auto& a : ParameterTypes)
		MessageTagNode->Parameters.Add(FMessageParameter{a->Name, a->Type});
	MessageTagNode->ResponseTypes.Empty(ResponseTypes.Num());
	for (auto& a : ResponseTypes)
		MessageTagNode->ResponseTypes.Add(FMessageParameter{a->Name, a->Type});
	if (Module.RenameTagInINI(TagToRename, NewTagName, MessageTagNode->Parameters, MessageTagNode->ResponseTypes))
	{
		OnMessageTagRenamed.ExecuteIfBound(TagToRename, NewTagName);
	}

	CloseContainingWindow();
}

void SRenameMessageTagDialog::OnRenameTextCommitted(const FText& InText, ETextCommit::Type InCommitType)
{
	if (InCommitType == ETextCommit::OnEnter && IsRenameEnabled())
	{
		RenameAndClose();
	}
}

FReply SRenameMessageTagDialog::OnRenameClicked()
{
	RenameAndClose();

	return FReply::Handled();
}

FReply SRenameMessageTagDialog::OnCancelClicked()
{
	CloseContainingWindow();

	return FReply::Handled();
}

void SRenameMessageTagDialog::CloseContainingWindow()
{
	TSharedPtr<SWindow> CurrentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());

	if (CurrentWindow.IsValid())
	{
		CurrentWindow->RequestDestroyWindow();
	}
}

TSharedRef<class ITableRow> SRenameMessageTagDialog::OnGenerateParameterRow(TSharedPtr<FMessageParameterDetail> InItem, const TSharedRef<STableViewBase>& OwnerTable, TWeakPtr<SListView<TSharedPtr<FMessageParameterDetail>>> WeakListView)
{
	return SNew(STableRow<TSharedPtr<FMessageParameterDetail>>, OwnerTable)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(0.1f)
		.Padding(0.f, 1.f, 0.f, 1.f)
		[
			SNew(SEditableTextBox)
			.Text_Lambda([InItem] { return FText::FromName(InItem->Name); })
			// .OnTextChanged_Lambda([InItem](const FText& Text) { InItem->Name = *Text.ToString(); })
			.OnTextCommitted_Lambda([InItem](const FText& Text, ETextCommit::Type InTextCommit) { InItem->Name = *Text.ToString(); })
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.IsEnabled(true)
		]
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 4.0f, 0.0f)
		.AutoWidth()
		[
			SAssignNew(PinTypeSelector, SPinTypeSelector, FGetPinTypeTree::CreateUObject(GetDefault<UEdGraphSchema_K2>(), &UEdGraphSchema_K2::GetVariableTypeTree))
			.TargetPinType_Lambda([this, InItem]() { return GetPinInfo(InItem); })
			.OnPinTypeChanged_Lambda([InItem, WeakListView](const FEdGraphPinType& Type) {
				// auto Widget = PinTypeSelector.Pin();
				if (InItem->PinType != Type)
				{
					InItem->PinType = Type;
					InItem->Type = GMPReflection::GetPinPropertyName(Type);
				}
				if (auto Widget = WeakListView.Pin())
					FSlateApplication::Get().SetKeyboardFocus(Widget->AsWidget(), EFocusCause::SetDirectly);
			})
			.Schema(GetDefault<UEdGraphSchema_K2>())
			.TypeTreeFilter(ETypeTreeFilter::None)
			.bAllowArrays(true)
			.IsEnabled(bAllowFullEdit)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.Padding(10, 0, 0, 0)
		.AutoWidth()
		[
			PropertyCustomizationHelpers::MakeClearButton(FSimpleDelegate::CreateLambda([this, InItem, WeakListView] { OnRemoveClicked(InItem, WeakListView); }), LOCTEXT("FunctionArgDetailsClearTooltip", "Remove this parameter."), bAllowFullEdit)
		]
	];
}

FReply SRenameMessageTagDialog::OnAddNewParameterButtonPressed()
{
	static FEdGraphPinType StringType;
	StringType.PinCategory = UEdGraphSchema_K2::PC_String;

	auto& Ref = Add_GetRef(ParameterTypes, MakeShared<FMessageParameterDetail>());
	Ref->Name = *FString::Printf(TEXT("Param%d"), ParameterTypes.Num() - 1);
	Ref->Type = TEXT("String");
	Ref->PinType = StringType;
	if (auto Widget = ListViewParameters.Pin())
		Widget->RequestListRefresh();
	return FReply::Handled();
}

FReply SRenameMessageTagDialog::OnAddNewResponeTypeButtonPressed()
{
	static FEdGraphPinType StringType;
	StringType.PinCategory = UEdGraphSchema_K2::PC_String;

	auto& Ref = Add_GetRef(ResponseTypes, MakeShared<FMessageParameterDetail>());
	Ref->Name = *FString::Printf(TEXT("Param%d"), ResponseTypes.Num() - 1);
	Ref->Type = TEXT("String");
	Ref->PinType = StringType;

	if (auto Widget = ListViewResponseTypes.Pin())
		Widget->RequestListRefresh();
	return FReply::Handled();
}

FEdGraphPinType SRenameMessageTagDialog::GetPinInfo(const TSharedPtr<FMessageParameterDetail>& InItem)
{
	return InItem->PinType;
	// 	FEdGraphPinType PinType;
	// 	bool ret = UnrealEditorUtils::PinTypeFromString(InItem->Type.ToString(), PinType);
	// 	return ret ? PinType : InItem->PinType;
}

void SRenameMessageTagDialog::OnRemoveClicked(const TSharedPtr<FMessageParameterDetail>& InItem, TWeakPtr<SListView<TSharedPtr<FMessageParameterDetail>>> WeakListView)
{
	auto Num = WeakListView == ListViewParameters ? ParameterTypes.Remove(InItem) : ResponseTypes.Remove(InItem);
	if (Num > 0)
	{
		if (auto Widget = WeakListView.Pin())
			Widget->RequestListRefresh();
	}
}
#undef LOCTEXT_NAMESPACE
