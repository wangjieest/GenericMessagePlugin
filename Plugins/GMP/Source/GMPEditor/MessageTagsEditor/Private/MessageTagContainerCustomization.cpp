// Copyright Epic Games, Inc. All Rights Reserved.

#include "MessageTagContainerCustomization.h"
#include "Widgets/Input/SComboButton.h"

#include "Widgets/Input/SButton.h"


#include "Editor.h"
#include "PropertyHandle.h"
#include "DetailWidgetRow.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SHyperlink.h"
#include "EditorFontGlyphs.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "UnrealCompatibility.h"

#define LOCTEXT_NAMESPACE "MessageTagContainerCustomization"

void FMessageTagContainerCustomization::CustomizeHeader(TSharedRef<class IPropertyHandle> InStructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructPropertyHandle = InStructPropertyHandle;

	FSimpleDelegate OnTagContainerChanged = FSimpleDelegate::CreateSP(this, &FMessageTagContainerCustomization::RefreshTagList);
	StructPropertyHandle->SetOnPropertyValueChanged(OnTagContainerChanged);

	BuildEditableContainerList();

	FUIAction SearchForReferencesAction(FExecuteAction::CreateSP(this, &FMessageTagContainerCustomization::OnWholeContainerSearchForReferences));

	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MaxDesiredWidth(512)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(EditButton, SComboButton)
					.OnGetMenuContent(this, &FMessageTagContainerCustomization::GetListContent)
					.OnMenuOpenChanged(this, &FMessageTagContainerCustomization::OnMessageTagListMenuOpenStateChanged)
					.ContentPadding(FMargin(2.0f, 2.0f))
					.MenuPlacement(MenuPlacement_BelowAnchor)
					.ButtonContent()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("MessageTagContainerCustomization_Edit", "Edit..."))
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SButton)
					.IsEnabled(!StructPropertyHandle->IsEditConst())
					.Text(LOCTEXT("MessageTagContainerCustomization_Clear", "Clear All"))
					.OnClicked(this, &FMessageTagContainerCustomization::OnClearAllButtonClicked)
					.Visibility(this, &FMessageTagContainerCustomization::GetClearAllVisibility)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBorder)
				.Padding(4.0f)
				.Visibility(this, &FMessageTagContainerCustomization::GetTagsListVisibility)
				[
					ActiveTags()
				]
			]
		]
#if UE_4_20_OR_LATER
		.AddCustomContextMenuAction(SearchForReferencesAction,
			LOCTEXT("WholeContainerSearchForReferences", "Search For References"),
			LOCTEXT("WholeContainerSearchForReferencesTooltip", "Find referencers that reference *any* of the tags in this container"),
			FSlateIcon())
#endif
			;

	GEditor->RegisterForUndo(this);
}

TSharedRef<SWidget> FMessageTagContainerCustomization::ActiveTags()
{	
	RefreshTagList();
	
	SAssignNew( TagListView, SListView<TSharedPtr<FMessageTag>> )
	.ListItemsSource(&TagList)
	.SelectionMode(ESelectionMode::None)
	.OnGenerateRow(this, &FMessageTagContainerCustomization::MakeListViewWidget);

	return TagListView->AsShared();
}

void FMessageTagContainerCustomization::RefreshTagList()
{
	// Rebuild Editable Containers as container references can become unsafe
	BuildEditableContainerList();

	// Build the set of tags on any instance, collapsing common tags together
	TSet<FMessageTag> CurrentTagSet;
	for (int32 ContainerIdx = 0; ContainerIdx < EditableContainers.Num(); ++ContainerIdx)
	{
		if (const FMessageTagContainer* Container = EditableContainers[ContainerIdx].TagContainer)
		{
			for (auto It = Container->CreateConstIterator(); It; ++It)
			{
				CurrentTagSet.Add(*It);
			}
		}
	}

	// Convert the set into pointers for the combo
	TagList.Empty(CurrentTagSet.Num());
	for (const FMessageTag& CurrentTag : CurrentTagSet)
	{
		TagList.Add(MakeShared<FMessageTag>(CurrentTag));
	}
	TagList.StableSort([](const TSharedPtr<FMessageTag>& One, const TSharedPtr<FMessageTag>& Two)
	{
		return *One < *Two;
	});

	// Refresh the slate list
	if( TagListView.IsValid() )
	{
		TagListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> FMessageTagContainerCustomization::MakeListViewWidget(TSharedPtr<FMessageTag> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedPtr<SWidget> TagItem;

	const FString TagName = Item->ToString();
	if (UMessageTagsManager::Get().ShowMessageTagAsHyperLinkEditor(TagName))
	{
		TagItem = SNew(SHyperlink)
			.Text(FText::FromString(TagName))
			.OnNavigate(this, &FMessageTagContainerCustomization::OnTagDoubleClicked, *Item.Get());
	}
	else
	{
		TagItem = SNew(STextBlock)
			.Text(FText::FromString(TagName));
	}

	return SNew( STableRow< TSharedPtr<FString> >, OwnerTable )
	[
		SNew(SBorder)
		.OnMouseButtonDown(this, &FMessageTagContainerCustomization::OnSingleTagMouseButtonPressed, TagName)
		.Padding(0.0f)
		.BorderImage(FAppStyle::GetBrush("NoBorder"))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0,0,2,0)
			[
				SNew(SButton)
				.IsEnabled(!StructPropertyHandle->IsEditConst())
				.ContentPadding(FMargin(0))
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Danger")
				.ForegroundColor(FSlateColor::UseForeground())
				.OnClicked(this, &FMessageTagContainerCustomization::OnRemoveTagClicked, *Item.Get())
				[
					SNew(STextBlock)
					.Font(FAppStyle::Get().GetFontStyle("FontAwesome.9"))
					.Text(FEditorFontGlyphs::Times)
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				TagItem.ToSharedRef()
			]
		]
	];
}

FReply FMessageTagContainerCustomization::OnSingleTagMouseButtonPressed(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, FString TagName)
{
	if (MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton))
	{
		FMenuBuilder MenuBuilder(/*bShouldCloseWindowAfterMenuSelection=*/ true, /*CommandList=*/ nullptr);

		FUIAction SearchForReferencesAction(FExecuteAction::CreateSP(this, &FMessageTagContainerCustomization::OnSingleTagSearchForReferences, TagName));

		MenuBuilder.BeginSection(NAME_None, FText::Format(LOCTEXT("SingleTagMenuHeading", "Tag Actions ({0})"), FText::AsCultureInvariant(TagName)));
		MenuBuilder.AddMenuEntry(
			LOCTEXT("SingleTagSearchForReferences", "Search For References"),
			FText::Format(LOCTEXT("SingleTagSearchForReferencesTooltip", "Find references to the tag {0}"), FText::AsCultureInvariant(TagName)),
			FSlateIcon(),
			SearchForReferencesAction);
		MenuBuilder.EndSection();

		// Spawn context menu
		FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
		FSlateApplication::Get().PushMenu(TagListView.ToSharedRef(), WidgetPath, MenuBuilder.MakeWidget(), MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void FMessageTagContainerCustomization::OnSingleTagSearchForReferences(FString TagName)
{
	FName TagFName(*TagName, FNAME_Find);
#if UE_4_23_OR_LATER
	if (FEditorDelegates::OnOpenReferenceViewer.IsBound() && !TagFName.IsNone())
#else
	if (!TagFName.IsNone())
#endif
	{
		TArray<FAssetIdentifier> AssetIdentifiers;
		AssetIdentifiers.Emplace(FMessageTag::StaticStruct(), TagFName);

		extern void MesageTagsEditor_SearchMessageReferences(const TArray<FAssetIdentifier>& AssetIdentifiers);
		MesageTagsEditor_SearchMessageReferences(AssetIdentifiers);
	}
}

void FMessageTagContainerCustomization::OnWholeContainerSearchForReferences()
{
#if UE_4_23_OR_LATER
	if (FEditorDelegates::OnOpenReferenceViewer.IsBound())
#endif
	{
		TArray<FAssetIdentifier> AssetIdentifiers;
		AssetIdentifiers.Reserve(TagList.Num());
		for (auto& TagPtr : TagList)
		{
			if (TagPtr->IsValid())
			{
				AssetIdentifiers.Emplace(FMessageTag::StaticStruct(), TagPtr->GetTagName());
			}
		}

		extern void MesageTagsEditor_SearchMessageReferences(const TArray<FAssetIdentifier>& AssetIdentifiers);
		MesageTagsEditor_SearchMessageReferences(AssetIdentifiers);
	}
}

void FMessageTagContainerCustomization::OnTagDoubleClicked(FMessageTag Tag)
{
	UMessageTagsManager::Get().NotifyMessageTagDoubleClickedEditor(Tag.ToString());
}

FReply FMessageTagContainerCustomization::OnRemoveTagClicked(FMessageTag Tag)
{
	TArray<FString> NewValues;
	for (int32 ContainerIdx = 0; ContainerIdx < EditableContainers.Num(); ++ContainerIdx)
	{
		FMessageTagContainer TagContainerCopy;
		if (const FMessageTagContainer* Container = EditableContainers[ContainerIdx].TagContainer)
		{
			TagContainerCopy = *Container;
		}
		TagContainerCopy.RemoveTag(Tag);

		NewValues.Add(TagContainerCopy.ToString());
	}

	{
		FScopedTransaction Transaction(LOCTEXT("RemoveMessageTagFromContainer", "Remove Message Tag"));
		for (int i = 0; i < NewValues.Num(); i++)
		{
			StructPropertyHandle->SetPerObjectValue(i, NewValues[i]);
		}
	}

	RefreshTagList();

	return FReply::Handled();
}

TSharedRef<SWidget> FMessageTagContainerCustomization::GetListContent()
{
	if (!StructPropertyHandle.IsValid() || StructPropertyHandle->GetProperty() == nullptr)
	{
		return SNullWidget::NullWidget;
	}

	FString Categories = UMessageTagsManager::Get().GetCategoriesMetaFromPropertyHandle(StructPropertyHandle);

	bool bReadOnly = StructPropertyHandle->IsEditConst();

	TSharedRef<SMessageTagWidget> TagWidget = SNew(SMessageTagWidget, EditableContainers)
		.Filter(Categories)
		.ReadOnly(bReadOnly)
		.TagContainerName(StructPropertyHandle->GetPropertyDisplayName().ToString())
		.OnTagChanged(this, &FMessageTagContainerCustomization::RefreshTagList)
		.PropertyHandle(StructPropertyHandle);

	LastTagWidget = TagWidget;

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.MaxHeight(400)
		[
			TagWidget
		];
}

void FMessageTagContainerCustomization::OnMessageTagListMenuOpenStateChanged(bool bIsOpened)
{
	if (bIsOpened)
	{
		TSharedPtr<SMessageTagWidget> TagWidget = LastTagWidget.Pin();
		if (TagWidget.IsValid())
		{
			EditButton->SetMenuContentWidgetToFocus(TagWidget->GetWidgetToFocusOnOpen());
		}
	}
}

FReply FMessageTagContainerCustomization::OnClearAllButtonClicked()
{
	{
		FScopedTransaction Transaction(LOCTEXT("MessageTagContainerCustomization_RemoveAllTags", "Remove All Message Tags"));

		for (int32 ContainerIdx = 0; ContainerIdx < EditableContainers.Num(); ++ContainerIdx)
		{
			FMessageTagContainer* Container = EditableContainers[ContainerIdx].TagContainer;

			if (Container)
			{
				FMessageTagContainer EmptyContainer;
				StructPropertyHandle->SetValueFromFormattedString(EmptyContainer.ToString());
			}
		}
	}
	RefreshTagList();
	return FReply::Handled();
}

EVisibility FMessageTagContainerCustomization::GetClearAllVisibility() const
{
	return TagList.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility FMessageTagContainerCustomization::GetTagsListVisibility() const
{
	return TagList.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

void FMessageTagContainerCustomization::PostUndo( bool bSuccess )
{
	if( bSuccess )
	{
		RefreshTagList();
	}
}

void FMessageTagContainerCustomization::PostRedo( bool bSuccess )
{
	if( bSuccess )
	{
		RefreshTagList();
	}
}

FMessageTagContainerCustomization::~FMessageTagContainerCustomization()
{
	GEditor->UnregisterForUndo(this);
}

void FMessageTagContainerCustomization::BuildEditableContainerList()
{
	EditableContainers.Empty();

	if( StructPropertyHandle.IsValid() )
	{
		TArray<void*> RawStructData;
		StructPropertyHandle->AccessRawData(RawStructData);

		for (int32 ContainerIdx = 0; ContainerIdx < RawStructData.Num(); ++ContainerIdx)
		{
			EditableContainers.Add(SMessageTagWidget::FEditableMessageTagContainerDatum(nullptr, (FMessageTagContainer*)RawStructData[ContainerIdx]));
		}
	}	
}

#undef LOCTEXT_NAMESPACE
