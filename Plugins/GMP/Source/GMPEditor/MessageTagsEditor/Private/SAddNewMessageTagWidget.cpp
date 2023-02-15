// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAddNewMessageTagWidget.h"

#include "DetailLayoutBuilder.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "GMPCore.h"
#include "MessageTagsEditorModule.h"
#include "MessageTagsManager.h"
#include "MessageTagsModule.h"
#include "MessageTagsSettings.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "SPinTypeSelector.h"
#include "Types/ISlateMetaData.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "AddNewMessageTagWidget"

struct FMessageParameterDetail : FMessageParameter
{
	FEdGraphPinType PinType;
};

SAddNewMessageTagWidget::~SAddNewMessageTagWidget()
{
	if (!GExitPurge)
	{
		IMessageTagsModule::OnTagSettingsChanged.RemoveAll(this);
	}
}

void SAddNewMessageTagWidget::Construct(const FArguments& InArgs)
{
	FText HintText = LOCTEXT("NewTagNameHint", "X.Y.Z");
	DefaultNewName = InArgs._NewTagName;
	if (DefaultNewName.IsEmpty() == false)
	{
		HintText = FText::FromString(DefaultNewName);
	}

	bAddingNewTag = false;
	bShouldGetKeyboardFocus = false;

	OnMessageTagAdded = InArgs._OnMessageTagAdded;
	IsValidTag = InArgs._IsValidTag;
	PopulateTagSources();

	IMessageTagsModule::OnTagSettingsChanged.AddRaw(this, &SAddNewMessageTagWidget::PopulateTagSources);

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
				.OnTextCommitted(this, &SAddNewMessageTagWidget::OnCommitNewTagName)
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
				.OnClicked(this, &SAddNewMessageTagWidget::OnAddNewParameterTypesButtonPressed)
			]
		]
		// Tag Parameters
		+ SVerticalBox::Slot()
		.AutoHeight()
		.VAlign(VAlign_Top)
		[
			SAssignNew(ListViewParameters, SListView<TSharedPtr<FMessageParameterDetail>>)
			.ItemHeight(24)
			.ListItemsSource(&ParameterTypes)  //The Items array is the source of this listview
			.OnGenerateRow(this, &SAddNewMessageTagWidget::OnGenerateParameterRow, ListViewParameters)
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
				.OnTextCommitted(this, &SAddNewMessageTagWidget::OnCommitNewTagName)
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
				.Text(LOCTEXT("AddNewResponseType", "New Response Type"))
				.OnClicked(this, &SAddNewMessageTagWidget::OnAddNewResponeTypesButtonPressed)
			]
		]
		// Respone Types
		+ SVerticalBox::Slot()
		.AutoHeight()
		.VAlign(VAlign_Top)
		[
			SAssignNew(ListViewResponseTypes, SListView<TSharedPtr<FMessageParameterDetail>>)
			.ItemHeight(24)
			.ListItemsSource(&ResponseTypes)  //The Items array is the source of this listview
			.OnGenerateRow(this, &SAddNewMessageTagWidget::OnGenerateParameterRow, ListViewResponseTypes)
			.SelectionMode(ESelectionMode::None)
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
				SAssignNew(TagSourcesComboBox, SComboBox<TSharedPtr<FName>>)
				.OptionsSource(&TagSources)
				.OnGenerateWidget(this, &SAddNewMessageTagWidget::OnGenerateTagSourcesComboBox)
				.ToolTipText(this, &SAddNewMessageTagWidget::CreateTagSourcesComboBoxToolTip)
				.ContentPadding(2.0f)
				.Content()
				[
					SNew(STextBlock)
					.Text(this, &SAddNewMessageTagWidget::CreateTagSourcesComboBoxContent)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FGMPStyle::Get(), "NoBorder")
				.Visibility(this, &SAddNewMessageTagWidget::OnGetTagSourceFavoritesVisibility)
				.OnClicked(this, &SAddNewMessageTagWidget::OnToggleTagSourceFavoriteClicked)
				.ToolTipText(LOCTEXT("ToggleFavoriteTooltip", "Toggle whether or not this tag source is your favorite source (new tags will go into your favorite source by default)"))
				.ContentPadding(0)
				[
					SNew(SImage)
					.Image(this, &SAddNewMessageTagWidget::OnGetTagSourceFavoriteImage)
				]
			]
		]

		// Add Tag Button
		+ SVerticalBox::Slot()
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
				.OnClicked(this, &SAddNewMessageTagWidget::OnAddNewTagButtonPressed)
			]
		]
	];

	Reset(EResetType::ResetAll);
}

EVisibility SAddNewMessageTagWidget::OnGetTagSourceFavoritesVisibility() const
{
	return (TagSources.Num() > 1) ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SAddNewMessageTagWidget::OnToggleTagSourceFavoriteClicked()
{
	const FName ActiveTagSource = *TagSourcesComboBox->GetSelectedItem().Get();
	const bool bWasFavorite = FMessageTagSource::GetFavoriteName() == ActiveTagSource;

	FMessageTagSource::SetFavoriteName(bWasFavorite ? NAME_None : ActiveTagSource);

	return FReply::Handled();
}

const FSlateBrush* SAddNewMessageTagWidget::OnGetTagSourceFavoriteImage() const
{
	const FName ActiveTagSource = *TagSourcesComboBox->GetSelectedItem().Get();
	const bool bIsFavoriteTagSource = FMessageTagSource::GetFavoriteName() == ActiveTagSource;

	return FGMPStyle::GetBrush(bIsFavoriteTagSource ? TEXT("PropertyWindow.Favorites_Enabled") : TEXT("PropertyWindow.Favorites_Disabled"));
}

void SAddNewMessageTagWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bShouldGetKeyboardFocus)
	{
		bShouldGetKeyboardFocus = false;
		FSlateApplication::Get().SetKeyboardFocus(TagNameTextBox.ToSharedRef(), EFocusCause::SetDirectly);
	}
}

void SAddNewMessageTagWidget::PopulateTagSources()
{
	UMessageTagsManager& Manager = UMessageTagsManager::Get();
	TagSources.Empty();

	FName DefaultSource = FMessageTagSource::GetDefaultName();

	// Always ensure that the default source is first
	TagSources.Add(MakeShareable(new FName(DefaultSource)));

	TArray<const FMessageTagSource*> Sources;
	Manager.FindTagSourcesWithType(EMessageTagSourceType::TagList, Sources);

	Algo::SortBy(Sources, &FMessageTagSource::SourceName, FNameLexicalLess());

	for (const FMessageTagSource* Source : Sources)
	{
		if (Source != nullptr && Source->SourceName != DefaultSource)
		{
			TagSources.Add(MakeShareable(new FName(Source->SourceName)));
		}
	}

	//Set selection to the latest added source
	if (TagSourcesComboBox.IsValid())
	{
		TagSourcesComboBox->SetSelectedItem(TagSources.Last());
	}
}

void SAddNewMessageTagWidget::Reset(EResetType ResetType)
{
	SetTagName();
	if (ResetType != EResetType::DoNotResetSource)
	{
		SelectTagSource();
	}
	TagCommentTextBox->SetText(FText());
}

void SAddNewMessageTagWidget::SetTagName(const FText& InName)
{
	TagNameTextBox->SetText(InName.IsEmpty() ? FText::FromString(DefaultNewName) : InName);
}

void SAddNewMessageTagWidget::SelectTagSource(const FName& InSource)
{
	// Attempt to find the location in our sources, otherwise just use the first one
	int32 SourceIndex = 0;

	if (!InSource.IsNone())
	{
		for (int32 Index = 0; Index < TagSources.Num(); ++Index)
		{
			TSharedPtr<FName> Source = TagSources[Index];

			if (Source.IsValid() && *Source.Get() == InSource)
			{
				SourceIndex = Index;
				break;
			}
		}
	}

	TagSourcesComboBox->SetSelectedItem(TagSources[SourceIndex]);
}

void SAddNewMessageTagWidget::OnCommitNewTagName(const FText& InText, ETextCommit::Type InCommitType)
{
	if (InCommitType == ETextCommit::OnEnter)
	{
		CreateNewMessageTag();
	}
}

FReply SAddNewMessageTagWidget::OnAddNewTagButtonPressed()
{
	CreateNewMessageTag();
	return FReply::Handled();
}

void SAddNewMessageTagWidget::AddSubtagFromParent(const FString& ParentTagName, const FName& ParentTagSource)
{
	FText SubtagBaseName = !ParentTagName.IsEmpty() ? FText::Format(FText::FromString(TEXT("{0}.")), FText::FromString(ParentTagName)) : FText();

	SetTagName(SubtagBaseName);
	SelectTagSource(ParentTagSource);

	bShouldGetKeyboardFocus = true;
}

void SAddNewMessageTagWidget::CreateNewMessageTag()
{
	UMessageTagsManager& Manager = UMessageTagsManager::Get();

	// Only support adding tags via ini file
	if (Manager.ShouldImportTagsFromINI() == false)
	{
		return;
	}

	if (TagSourcesComboBox->GetSelectedItem().Get() == nullptr)
	{
		return;
	}

	FText TagNameAsText = TagNameTextBox->GetText();
	FString TagName = TagNameAsText.ToString();
	FString TagComment = TagCommentTextBox->GetText().ToString();
	FName TagSource = *TagSourcesComboBox->GetSelectedItem().Get();

	if (TagName.IsEmpty())
	{
		return;
	}

	// check to see if this is a valid tag
	// first check the base rules for all tags then look for any additional rules in the delegate
	FText ErrorMsg;
	if (!UMessageTagsManager::Get().IsValidMessageTagString(TagName, &ErrorMsg) || (IsValidTag.IsBound() && !IsValidTag.Execute(TagName, &ErrorMsg)))
	{
		FText MessageTitle(LOCTEXT("InvalidTag", "Invalid Tag"));
		FMessageDialog::Open(EAppMsgType::Ok, ErrorMsg, &MessageTitle);
		return;
	}

	// set bIsAddingNewTag, this guards against the window closing when it loses focus due to source control checking out a file
	TGuardValue<bool> Guard(bAddingNewTag, true);

	TArray<FMessageParameter> ParamterData;
	ParamterData.Empty(ParameterTypes.Num());
	for (auto& a : ParameterTypes)
	{
		ParamterData.Add(FMessageParameter{a->Name, a->Type});
	}

	TArray<FMessageParameter> ResponeTypeData;
	ResponeTypeData.Empty(ResponseTypes.Num());
	for (auto& a : ResponseTypes)
	{
		ResponeTypeData.Add(FMessageParameter{a->Name, a->Type});
	}

	IMessageTagsEditorModule::Get().AddNewMessageTagToINI(TagName, TagComment, TagSource, false, true, ParamterData, ResponeTypeData);

	OnMessageTagAdded.ExecuteIfBound(TagName, TagComment, TagSource);

	Reset(EResetType::DoNotResetSource);
}

TSharedRef<SWidget> SAddNewMessageTagWidget::OnGenerateTagSourcesComboBox(TSharedPtr<FName> InItem)
{
	return SNew(STextBlock)
			.Text(FText::FromName(*InItem.Get()));
}

FText SAddNewMessageTagWidget::CreateTagSourcesComboBoxContent() const
{
	const bool bHasSelectedItem = TagSourcesComboBox.IsValid() && TagSourcesComboBox->GetSelectedItem().IsValid();

	return bHasSelectedItem ? FText::FromName(*TagSourcesComboBox->GetSelectedItem().Get()) : LOCTEXT("NewTagLocationNotSelected", "Not selected");
}

FText SAddNewMessageTagWidget::CreateTagSourcesComboBoxToolTip() const
{
	const bool bHasSelectedItem = TagSourcesComboBox.IsValid() && TagSourcesComboBox->GetSelectedItem().IsValid();

	if (bHasSelectedItem)
	{
		UMessageTagsManager& Manager = UMessageTagsManager::Get();
		const FMessageTagSource* Source = Manager.FindTagSource(*TagSourcesComboBox->GetSelectedItem().Get());
		if (Source)
		{
			FString FilePath = Source->GetConfigFileName();

			if (FPaths::IsUnderDirectory(FilePath, FPaths::ProjectDir()))
			{
				FPaths::MakePathRelativeTo(FilePath, *FPaths::ProjectDir());
			}
			return FText::FromString(FilePath);
		}
	}

	return FText();
}

TSharedRef<class ITableRow> SAddNewMessageTagWidget::OnGenerateParameterRow(TSharedPtr<FMessageParameterDetail> InItem, const TSharedRef<STableViewBase>& OwnerTable, TWeakPtr<SListView<TSharedPtr<FMessageParameterDetail>>> WeakListView)
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
			.TargetPinType_Lambda([this, InItem] { return GetPinInfo(InItem); })
			.OnPinTypeChanged_Lambda([this, InItem, WeakListView](const FEdGraphPinType& Type) {
				if (InItem->PinType != Type)
				{
					InItem->PinType = Type;
					InItem->Type = GMPReflection::GetPinPropertyName(Type);
				}
				// auto Widget = PinTypeSelector.Pin();
				// auto Captor = FSlateApplication::Get().GetCursorUser()->GetCursorCaptor();
				// if (Captor.IsValid() && Captor->GetTypeAsString() == TEXT("STableRow<TSharedPtr<EPinContainerType>>"))
				// {
				// 	class SPinContainerListView : public STableRow<TSharedPtr<EPinContainerType>>
				// 	{
				// 	public:
				// 	using STableRow<TSharedPtr<EPinContainerType>>::OwnerTablePtr;
				// 	};
				//
				// 	auto TableList = StaticCastSharedRef<STableViewBase>(StaticCastSharedPtr<SPinContainerListView>(Captor)->OwnerTablePtr.Pin()->AsWidget());
				// 	TableList->GetParentWidget()->SetVisibility(EVisibility::Hidden);
				// }

				if (auto Widget = WeakListView.Pin())
				{
					// FSlateApplication::Get().SetKeyboardFocus(Widget->AsWidget(), EFocusCause::SetDirectly);
				}
			})
			.Schema(GetDefault<UEdGraphSchema_K2>())
			.TypeTreeFilter(ETypeTreeFilter::None)
			.bAllowArrays(true)
			.IsEnabled(true)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.Padding(10, 0, 0, 0)
		.AutoWidth()
		[
			PropertyCustomizationHelpers::MakeClearButton(FSimpleDelegate::CreateLambda([this, InItem, WeakListView] { OnRemoveClicked(InItem, WeakListView); }), LOCTEXT("FunctionArgDetailsClearTooltip", "Remove this parameter."), true)
		]
	];
}

FReply SAddNewMessageTagWidget::OnAddNewParameterTypesButtonPressed()
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

FReply SAddNewMessageTagWidget::OnAddNewResponeTypesButtonPressed()
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

const FEdGraphPinType& SAddNewMessageTagWidget::GetPinInfo(const TSharedPtr<FMessageParameterDetail>& InItem)
{
	return InItem->PinType;
	// 	FEdGraphPinType PinType;
	// 	bool ret = UnrealEditorUtils::PinTypeFromString(InItem->Type.ToString(), PinType);
	// 	return ret ? PinType : InItem->PinType;
}

void SAddNewMessageTagWidget::OnRemoveClicked(const TSharedPtr<FMessageParameterDetail>& InItem, TWeakPtr<SListView<TSharedPtr<FMessageParameterDetail>>> WeakListView)
{
	auto Num = WeakListView == ListViewParameters ? ParameterTypes.Remove(InItem) : ResponseTypes.Remove(InItem);
	if (Num > 0)
	{
		if (auto Widget = WeakListView.Pin())
			Widget->RequestListRefresh();
	}
}

#undef LOCTEXT_NAMESPACE
