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
#include "Widgets/Input/SButton.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "SPinTypeSelector.h"
#include "Types/ISlateMetaData.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "MessageTagsManager.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

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
	bRestrictedTags = InArgs._RestrictedTags;

	OnMessageTagAdded = InArgs._OnMessageTagAdded;
	IsValidTag = InArgs._IsValidTag;
	PopulateTagSources();

	IMessageTagsModule::OnTagSettingsChanged.AddRaw(this, &SAddNewMessageTagWidget::PopulateTagSources);

	ChildSlot
	[
		SNew(SBox)
		.Padding(InArgs._Padding)
		[
			SNew(SGridPanel)
			.FillColumn(1, 1.0)
			
			// Tag Name
			+ SGridPanel::Slot(0, 0)
			.Padding(2)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle( TEXT("PropertyWindow.NormalFont")))
				.Text(LOCTEXT("NewTagName", "Name:"))
			]
			+ SGridPanel::Slot(1, 0)
			.Padding(2)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Fill)
			[
				SAssignNew(TagNameTextBox, SEditableTextBox)
				.HintText(HintText)
				.Font(FAppStyle::GetFontStyle( TEXT("PropertyWindow.NormalFont")))
				.OnTextCommitted(this, &SAddNewMessageTagWidget::OnCommitNewTagName)
			]
			// Add Tag Parameters
			+ SGridPanel::Slot(0, 1)
			.ColumnSpan(2)
			.VAlign(VAlign_Top)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(2.0f, 4.0f)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ParameterTypes", "ParameterTypes"))
				]

				+ SHorizontalBox::Slot()
				.Padding(2.0f, 2.0f)
				.FillWidth(1.0f)
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.Text(LOCTEXT("AddNewParameterType", "New Parameter Type"))
					.OnClicked(this, &SAddNewMessageTagWidget::OnAddNewParameterTypesButtonPressed)
				]
			]
			// Tag Parameters
			+ SGridPanel::Slot(0, 2)
			.ColumnSpan(2)
			.VAlign(VAlign_Top)
			[
				SAssignNew(ListViewParameters, SListView<TSharedPtr<FMessageParameterDetail>>)
				#if !UE_5_05_OR_LATER
				.ItemHeight(24)
				#endif
				.ListItemsSource(&ParameterTypes)  //The Items array is the source of this listview
				.OnGenerateRow(this, &SAddNewMessageTagWidget::OnGenerateParameterRow, false, ListViewParameters)
				.SelectionMode(ESelectionMode::None)
			]
			// Tag Comment
			+ SGridPanel::Slot(0, 3)
			.Padding(2)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle( TEXT("PropertyWindow.NormalFont")))
				.Text(LOCTEXT("TagComment", "DevComment:"))
			]
			+ SGridPanel::Slot(1, 3)
			.Padding(2)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Fill)
			[
				SAssignNew(TagCommentTextBox, SEditableTextBox)
				.HintText(LOCTEXT("TagCommentHint", "DevComment"))
				.Font(FAppStyle::GetFontStyle( TEXT("PropertyWindow.NormalFont")))
					.OnTextCommitted(this, &SAddNewMessageTagWidget::OnCommitNewTagName)
			]
			// Add Respone Types
			+ SGridPanel::Slot(0, 4)
			.ColumnSpan(2)
			.VAlign(VAlign_Top)
			.HAlign(HAlign_Fill)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(2.0f, 4.0f)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ResponseTypes", "ResponseTypes"))
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
			+ SGridPanel::Slot(0, 5)
			.ColumnSpan(2)
			.VAlign(VAlign_Top)
			[
				SAssignNew(ListViewResponseTypes, SListView<TSharedPtr<FMessageParameterDetail>>)
				#if !UE_5_05_OR_LATER
				.ItemHeight(24)
				#endif
				.ListItemsSource(&ResponseTypes)  //The Items array is the source of this listview
				.OnGenerateRow(this, &SAddNewMessageTagWidget::OnGenerateParameterRow, true, ListViewResponseTypes)
				.SelectionMode(ESelectionMode::None)
			]
			// Tag Location
			+ SGridPanel::Slot(0, 6)
			.Padding(2)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CreateTagSource", "Source:"))
				.Font(FAppStyle::GetFontStyle( TEXT("PropertyWindow.NormalFont")))
			]
			+ SGridPanel::Slot(1, 6)
			.Padding(2)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Fill)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(TagSourcesComboBox, SComboBox<TSharedPtr<FString>>)
					.OptionsSource(&TagSources)
					.InitiallySelectedItem(TagSources.Num() > 0 ? TagSources[0] : TSharedPtr<FString>())
					.OnGenerateWidget(this, &SAddNewMessageTagWidget::OnGenerateTagSourcesComboBox)
					.ToolTipText(this, &SAddNewMessageTagWidget::CreateTagSourcesComboBoxToolTip)
					.Content()
					[
						SNew(STextBlock)
						.Text(this, &SAddNewMessageTagWidget::CreateTagSourcesComboBoxContent)
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0, 0, 0)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle( FAppStyle::Get(), "NoBorder" )
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
			+ SGridPanel::Slot(0, 7)
			.ColumnSpan(2)
			.Padding(InArgs._AddButtonPadding)
			.HAlign(HAlign_Right)
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
	const FName ActiveTagSource = GetSelectedTagSource();
	const bool bWasFavorite = FMessageTagSource::GetFavoriteName() == ActiveTagSource;

	FMessageTagSource::SetFavoriteName(bWasFavorite ? NAME_None : ActiveTagSource);

	return FReply::Handled();
}

const FSlateBrush* SAddNewMessageTagWidget::OnGetTagSourceFavoriteImage() const
{
	const FName ActiveTagSource = GetSelectedTagSource();
	const bool bIsFavoriteTagSource = FMessageTagSource::GetFavoriteName() == ActiveTagSource;

	return FGMPStyle::GetBrush(bIsFavoriteTagSource ? TEXT("PropertyWindow.Favorites_Enabled") : TEXT("PropertyWindow.Favorites_Disabled"));
}

void SAddNewMessageTagWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bShouldGetKeyboardFocus)
	{
		bShouldGetKeyboardFocus = false;
		FSlateApplication::Get().SetKeyboardFocus(TagNameTextBox.ToSharedRef(), EFocusCause::SetDirectly);
		FSlateApplication::Get().SetUserFocus(0, TagNameTextBox.ToSharedRef());
	}
}

void SAddNewMessageTagWidget::PopulateTagSources()
{
	const UMessageTagsManager& Manager = UMessageTagsManager::Get();
	TagSources.Empty();

	TArray<const FMessageTagSource*> Sources;

	if (bRestrictedTags)
	{
		Manager.GetRestrictedTagSources(Sources);

		// Add the placeholder source if no other sources exist
		if (Sources.Num() == 0)
		{
			TagSources.Add(MakeShareable(new FString()));
		}

		for (const FMessageTagSource* Source : Sources)
		{
			if (Source != nullptr && !Source->SourceName.IsNone())
			{
				TagSources.Add(MakeShareable(new FString(Source->SourceName.ToString())));
			}
		}
	}
	else
	{
		const FName DefaultSource = FMessageTagSource::GetDefaultName();

		// Always ensure that the default source is first
		TagSources.Add( MakeShareable( new FString( DefaultSource.ToString() ) ) );

		Manager.FindTagSourcesWithType(EMessageTagSourceType::TagList, Sources);

		Algo::SortBy(Sources, &FMessageTagSource::SourceName, FNameLexicalLess());

		for (const FMessageTagSource* Source : Sources)
		{
			if (Source != nullptr && Source->SourceName != DefaultSource)
			{
				TagSources.Add(MakeShareable(new FString(Source->SourceName.ToString())));
			}
		}

		//Set selection to the latest added source
		if (TagSourcesComboBox != nullptr)
		{
			TagSourcesComboBox->SetSelectedItem(TagSources.Last());
		}
	}
}

void SAddNewMessageTagWidget::Reset(EResetType ResetType)
{
	SetTagName();
	if (ResetType != EResetType::DoNotResetSource)
	{
		SelectTagSource(FMessageTagSource::GetFavoriteName());
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
	int32 SourceIndex = INDEX_NONE;

	if (!InSource.IsNone())
	{
		for (int32 Index = 0; Index < TagSources.Num(); ++Index)
		{
			TSharedPtr<FString> Source = TagSources[Index];

			if (Source.IsValid() && *Source.Get() == InSource.ToString())
			{
				SourceIndex = Index;
				break;
			}
		}
	}

	if (SourceIndex != INDEX_NONE && TagSourcesComboBox.IsValid())
	{
		TagSourcesComboBox->SetSelectedItem(TagSources[SourceIndex]);
	}
}

FName SAddNewMessageTagWidget::GetSelectedTagSource() const
{
	const bool bHasSelectedItem = TagSourcesComboBox.IsValid() && TagSourcesComboBox->GetSelectedItem().IsValid();

	if (bHasSelectedItem)
	{
		// Convert to FName which the rest of the API expects
		return FName(**TagSourcesComboBox->GetSelectedItem().Get());
	}

	return NAME_None;
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

void SAddNewMessageTagWidget::AddDuplicate(const FString& InParentTagName, const FName& InParentTagSource)
{
	SetTagName(FText::FromString(InParentTagName));
	SelectTagSource(InParentTagSource);

	bShouldGetKeyboardFocus = true;
}

void SAddNewMessageTagWidget::CreateNewMessageTag()
{
	if (bRestrictedTags)
	{
		ValidateNewRestrictedTag();
		return;
	}

	if (NotificationItem.IsValid())
	{
		NotificationItem->SetVisibility(EVisibility::Collapsed);
	}

	UMessageTagsManager& Manager = UMessageTagsManager::Get();

	// Only support adding tags via ini file
	if (Manager.ShouldImportTagsFromINI() == false)
	{
		return;
	}

	if (TagSourcesComboBox->GetSelectedItem().Get() == nullptr)
	{
		FNotificationInfo Info(LOCTEXT("NoTagSource", "You must specify a source file for message tags."));
		Info.ExpireDuration = 10.f;
		Info.bUseSuccessFailIcons = true;
		Info.Image = FAppStyle::GetBrush(TEXT("MessageLog.Error"));
		NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
		
		return;
	}

	const FText TagNameAsText = TagNameTextBox->GetText();
	FString TagName = TagNameAsText.ToString();
	const FString TagComment = TagCommentTextBox->GetText().ToString();
	const FName TagSource = GetSelectedTagSource();

	if (TagName.IsEmpty())
	{
		FNotificationInfo Info(LOCTEXT("NoTagName", "You must specify tag name."));
		Info.ExpireDuration = 10.f;
		Info.bUseSuccessFailIcons = true;
		Info.Image = FAppStyle::GetBrush(TEXT("MessageLog.Error"));
		NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);

		return;
	}

	// check to see if this is a valid tag
	// first check the base rules for all tags then look for any additional rules in the delegate
	FText ErrorMsg;
	if (!UMessageTagsManager::Get().IsValidMessageTagString(TagName, &ErrorMsg) ||
		(IsValidTag.IsBound() && !IsValidTag.Execute(TagName, &ErrorMsg))
		)
	{
		FNotificationInfo Info(ErrorMsg);
		Info.ExpireDuration = 10.f;
		Info.bUseSuccessFailIcons = true;
		Info.Image = FAppStyle::GetBrush(TEXT("MessageLog.Error"));
		NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);

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


void SAddNewMessageTagWidget::ValidateNewRestrictedTag()
{
	UMessageTagsManager& Manager = UMessageTagsManager::Get();

	FString TagName = TagNameTextBox->GetText().ToString();
	FString TagComment = TagCommentTextBox->GetText().ToString();
	const FName TagSource = GetSelectedTagSource();

	if (TagSource == NAME_None)
	{
		FNotificationInfo Info(LOCTEXT("NoRestrictedSource", "You must specify a source file for restricted Message tags."));
		Info.ExpireDuration = 10.f;
		Info.bUseSuccessFailIcons = true;
		Info.Image = FAppStyle::GetBrush(TEXT("MessageLog.Error"));

		NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);

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
			Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("RestrictedTagPopupButtonAccept", "Yes"), FText(), FSimpleDelegate::CreateSP(this, &SAddNewMessageTagWidget::CreateNewRestrictedMessageTag), SNotificationItem::CS_None));
			Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("RestrictedTagPopupButtonReject", "No"), FText(), FSimpleDelegate::CreateSP(this, &SAddNewMessageTagWidget::CancelNewTag), SNotificationItem::CS_None));

			NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
		}
	}
	else
	{
		CreateNewRestrictedMessageTag();
	}
}

void SAddNewMessageTagWidget::CreateNewRestrictedMessageTag()
{
	if (NotificationItem.IsValid())
	{
		NotificationItem->SetVisibility(EVisibility::Collapsed);
	}

	const UMessageTagsManager& Manager = UMessageTagsManager::Get();

	// Only support adding tags via ini file
	if (Manager.ShouldImportTagsFromINI() == false)
	{
		return;
	}

	const FString TagName = TagNameTextBox->GetText().ToString();
	const FString TagComment = TagCommentTextBox->GetText().ToString();
	const bool bAllowNonRestrictedChildren = true; // can be changed later
	const FName TagSource = GetSelectedTagSource();

	if (TagName.IsEmpty())
	{
		return;
	}

	// set bIsAddingNewTag, this guards against the window closing when it loses focus due to source control checking out a file
	TGuardValue<bool>	Guard(bAddingNewTag, true);

	IMessageTagsEditorModule::Get().AddNewMessageTagToINI(TagName, TagComment, TagSource, true, bAllowNonRestrictedChildren);

	OnMessageTagAdded.ExecuteIfBound(TagName, TagComment, TagSource);

	Reset(EResetType::DoNotResetSource);
}

void SAddNewMessageTagWidget::CancelNewTag()
{
	if (NotificationItem.IsValid())
	{
		NotificationItem->SetVisibility(EVisibility::Collapsed);
	}
}

TSharedRef<SWidget> SAddNewMessageTagWidget::OnGenerateTagSourcesComboBox(TSharedPtr<FString> InItem)
{
	return SNew(STextBlock)
		.Text(FText::FromString(*InItem.Get()));
}

FText SAddNewMessageTagWidget::CreateTagSourcesComboBoxContent() const
{
	const bool bHasSelectedItem = TagSourcesComboBox.IsValid() && TagSourcesComboBox->GetSelectedItem().IsValid();

	return bHasSelectedItem ? FText::FromString(*TagSourcesComboBox->GetSelectedItem().Get()) : LOCTEXT("NewTagLocationNotSelected", "Not selected");
}

FText SAddNewMessageTagWidget::CreateTagSourcesComboBoxToolTip() const
{
	const FName ActiveTagSource = GetSelectedTagSource();
	if (!ActiveTagSource.IsNone())
	{
		UMessageTagsManager& Manager = UMessageTagsManager::Get();
		const FMessageTagSource* Source = Manager.FindTagSource(ActiveTagSource);
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

TSharedRef<class ITableRow> SAddNewMessageTagWidget::OnGenerateParameterRow(TSharedPtr<FMessageParameterDetail> InItem,
																			const TSharedRef<STableViewBase>& OwnerTable,
																			bool bIsResponse,
																			TWeakPtr<SListView<TSharedPtr<FMessageParameterDetail>>> WeakListView)
{
	auto K2Schema = GetDefault<UEdGraphSchema_K2>();
	auto GetPinTypeTree = CreateWeakLambda(K2Schema, [K2Schema](TArray<FPinTypeTreeItem>& TypeTree, ETypeTreeFilter Filter) {
		K2Schema->GetVariableTypeTree(TypeTree, Filter);

#if UE_5_00_OR_LATER && !GMP_FORCE_DOUBLE_PROPERTY
		FName PC_Real = UEdGraphSchema_K2::PC_Real;
		auto Index = TypeTree.IndexOfByPredicate([&](const FPinTypeTreeItem& Item) { return Item->GetPinType(false).PinCategory == PC_Real; });
		if (Index != INDEX_NONE)
		{
			TypeTree.RemoveAt(Index);

			auto DoubleInfo = MakeShared<UEdGraphSchema_K2::FPinTypeTreeInfo>(LOCTEXT("Double", "Double"), PC_Real, K2Schema, LOCTEXT("DoubleType", "Real (double-precision)"));
			DoubleInfo->SetPinSubTypeCategory(UEdGraphSchema_K2::PC_Double);
			TypeTree.Insert(DoubleInfo, Index);

			auto FloatInfo = MakeShared<UEdGraphSchema_K2::FPinTypeTreeInfo>(LOCTEXT("Float", "Float"), PC_Real, K2Schema, LOCTEXT("FloatType", "Real (single-precision)"));
			FloatInfo->SetPinSubTypeCategory(UEdGraphSchema_K2::PC_Float);
			TypeTree.Insert(FloatInfo, Index);
		}
#endif
	});

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
			SAssignNew(PinTypeSelector, SPinTypeSelector, GetPinTypeTree)
			.TargetPinType_Lambda([this, InItem] { return GetPinInfo(InItem); })
			.OnPinTypeChanged_Lambda([this, InItem, bIsResponse, WeakListView](const FEdGraphPinType& Type) {
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
			PropertyCustomizationHelpers::MakeClearButton(FSimpleDelegate::CreateLambda([this, InItem, bIsResponse, WeakListView] { OnRemoveClicked(InItem, bIsResponse, WeakListView); }),
														  LOCTEXT("FunctionArgDetailsClearTooltip", "Remove this parameter."),
														  true)
		]
	];
}

FReply SAddNewMessageTagWidget::OnAddNewParameterTypesButtonPressed()
{
	static FEdGraphPinType StringType;
	StringType.PinCategory = UEdGraphSchema_K2::PC_String;
	auto& Ref = Add_GetRef(ParameterTypes, MakeShared<FMessageParameterDetail>());
	Ref->Name = FName("Param", ParameterTypes.Num() - 1);
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
	Ref->Name = FName("Param", ResponseTypes.Num() - 1);
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

void SAddNewMessageTagWidget::OnRemoveClicked(const TSharedPtr<FMessageParameterDetail>& InItem, bool bIsResponse, TWeakPtr<SListView<TSharedPtr<FMessageParameterDetail>>> WeakListView)
{
	if (!bIsResponse)
	{
		if (ParameterTypes.Remove(InItem) > 0)
		{
			if (auto Widget = ListViewParameters.Pin())
				Widget->RequestListRefresh();
		}
	}
	else
	{
		if (ResponseTypes.Remove(InItem) > 0)
		{
			if (auto Widget = ListViewResponseTypes.Pin())
				Widget->RequestListRefresh();
		}
	}
}

#undef LOCTEXT_NAMESPACE
