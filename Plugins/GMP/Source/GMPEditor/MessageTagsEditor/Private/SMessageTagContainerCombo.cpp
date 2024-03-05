// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMessageTagContainerCombo.h"

#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_NEWER_THAN(5, 0, 0)

#include "DetailLayoutBuilder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SComboButton.h"
#include "MessageTagStyle.h"
#include "SMessageTagPicker.h"
#include "Framework/Application/SlateApplication.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "HAL/PlatformApplicationMisc.h"
#include "MessageTagEditorUtilities.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "MessageTagContainerCombo"

//------------------------------------------------------------------------------
// SMessageTagContainerCombo
//------------------------------------------------------------------------------

SLATE_IMPLEMENT_WIDGET(SMessageTagContainerCombo)
void SMessageTagContainerCombo::PrivateRegisterAttributes(FSlateAttributeInitializer& AttributeInitializer)
{
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION_WITH_NAME(AttributeInitializer, "TagContainer", TagContainerAttribute, EInvalidateWidgetReason::Layout)
		.OnValueChanged(FSlateAttributeDescriptor::FAttributeValueChangedDelegate::CreateLambda([](SWidget& Widget)
			{
				static_cast<SMessageTagContainerCombo&>(Widget).RefreshTagContainers();
			}));
}

SMessageTagContainerCombo::SMessageTagContainerCombo()
	: TagContainerAttribute(*this)
{
}

SMessageTagContainerCombo::~SMessageTagContainerCombo()
{
	if (bRegisteredForUndo)
	{
		GEditor->UnregisterForUndo(this);
	}
}

void SMessageTagContainerCombo::Construct(const FArguments& InArgs)
{
	Filter = InArgs._Filter;
	SettingsName = InArgs._SettingsName;
	bIsReadOnly = InArgs._ReadOnly;
	OnTagContainerChanged = InArgs._OnTagContainerChanged;
	PropertyHandle = InArgs._PropertyHandle;

	if (PropertyHandle.IsValid())
	{
		PropertyHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &SMessageTagContainerCombo::RefreshTagContainers));
		RefreshTagContainers();
		GEditor->RegisterForUndo(this);
		bRegisteredForUndo = true;

		if (Filter.IsEmpty())
		{
			Filter = UMessageTagsManager::Get().GetCategoriesMetaFromPropertyHandle(PropertyHandle);
		}
		bIsReadOnly = PropertyHandle->IsEditConst();
	}
	else
	{
		TagContainerAttribute.Assign(*this, InArgs._TagContainer);
		CachedTagContainers.Add(TagContainerAttribute.Get());
	}

	TWeakPtr<SMessageTagContainerCombo> WeakSelf = StaticCastWeakPtr<SMessageTagContainerCombo>(AsWeak());

	TagListView = SNew(SListView<TSharedPtr<FEditableItem>>)
		.ListItemsSource(&TagsToEdit)
		.SelectionMode(ESelectionMode::None)
		.ItemHeight(23.0f)
		.ListViewStyle(&FAppStyle::Get().GetWidgetStyle<FTableViewStyle>("SimpleListView"))
		.OnGenerateRow(this, &SMessageTagContainerCombo::MakeTagListViewRow)
		.Visibility_Lambda([WeakSelf]()
		{
			if (const TSharedPtr<SMessageTagContainerCombo> Self = WeakSelf.Pin())
			{
				return Self->TagsToEdit.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
			}
			return EVisibility::Collapsed;
		});

	ChildSlot
	[
		SNew(SHorizontalBox)
			
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Top)
		[
			SAssignNew(ComboButton, SComboButton)
			.ComboButtonStyle(FMessageTagStyle::Get(), "MessageTagsContainer.ComboButton")
			.IsEnabled(this, &SMessageTagContainerCombo::IsValueEnabled)
			.HasDownArrow(true)
			.VAlign(VAlign_Top)
			.ContentPadding(0)
			.OnMenuOpenChanged(this, &SMessageTagContainerCombo::OnMenuOpenChanged)
			.OnGetMenuContent(this, &SMessageTagContainerCombo::OnGetMenuContent)
			.ButtonContent()
			[
				SNew(SHorizontalBox)
				
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Top)
				[
					SNew(SBorder)
					.Padding(FMargin(6,2))
					.BorderImage(FMessageTagStyle::GetBrush("MessageTags.Container"))
					[
						SNew(SHorizontalBox)

						// Tag list
						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Top)
						.AutoWidth()
						[
							TagListView.ToSharedRef()
						]
						
						// Empty indicator
						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.AutoWidth()
						.Padding(FMargin(4, 2))
						[
							SNew(SBox)
							.HeightOverride(18.0f) // Same is SMessageTagChip height
							.VAlign(VAlign_Center)
							.Padding(0, 0, 8, 0)
							.Visibility_Lambda([WeakSelf]()
							{
								if (const TSharedPtr<SMessageTagContainerCombo> Self = WeakSelf.Pin())
								{
									return Self->TagsToEdit.Num() > 0 ? EVisibility::Collapsed : EVisibility::Visible;
								}
								return EVisibility::Collapsed;
							})
							[
								SNew(STextBlock)
								.ColorAndOpacity(FSlateColor::UseSubduedForeground())
								.Font(FAppStyle::GetFontStyle( TEXT("PropertyWindow.NormalFont")))
								.Text(LOCTEXT("MessageTagContainerCombo_Empty", "Empty"))
								.ToolTipText(LOCTEXT("MessageTagContainerCombo_EmptyTooltip", "Empty Message Tag container"))
							]
						]
					]
				]
			]
		]
	];
}

bool SMessageTagContainerCombo::IsValueEnabled() const
{
	if (PropertyHandle.IsValid())
	{
		return !PropertyHandle->IsEditConst();
	}

	return !bIsReadOnly;
}

TSharedRef<ITableRow> SMessageTagContainerCombo::MakeTagListViewRow(TSharedPtr<FEditableItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FEditableItem>>, OwnerTable)
		.Style(&FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("SimpleTableView.Row"))
		.Padding(FMargin(0,2))
		[
			SNew(SMessageTagChip)
			.ReadOnly(bIsReadOnly)
			.ShowClearButton(true)
			.Text(FText::FromName(Item->Tag.GetTagName()))
			.ToolTipText(FText::FromName(Item->Tag.GetTagName()))
			.IsSelected(!Item->bMultipleValues)
			.OnClearPressed(this, &SMessageTagContainerCombo::OnClearTagClicked, Item->Tag)
			.OnEditPressed(this, &SMessageTagContainerCombo::OnEditClicked, Item->Tag)
			.OnMenu(this, &SMessageTagContainerCombo::OnTagMenu, Item->Tag)
		];
}

void SMessageTagContainerCombo::OnMenuOpenChanged(const bool bOpen)
{
	if (bOpen && TagPicker.IsValid())
	{
		if (!TagToHilight.IsValid() && TagsToEdit.Num() > 0)
		{
			TagToHilight = TagsToEdit[0]->Tag;
		}
		TagPicker->RequestScrollToView(TagToHilight);
	}
	// Reset tag to hilight
	TagToHilight = FMessageTag();
}

TSharedRef<SWidget> SMessageTagContainerCombo::OnGetMenuContent()
{
	// If property is not set, we'll put the edited tag from attribute into a container and use that for picking.
	TArray<FMessageTagContainer> TagContainersToEdit;
	if (!PropertyHandle.IsValid() && CachedTagContainers.Num() == 1)
	{
		CachedTagContainers[0] = TagContainerAttribute.Get();
		TagContainersToEdit.Add(CachedTagContainers[0]);
	}

	const bool bIsPickerReadOnly = !IsValueEnabled();

	TagPicker = SNew(SMessageTagPicker)
		.Filter(Filter)
		.SettingsName(SettingsName)
		.ReadOnly(bIsPickerReadOnly)
		.ShowMenuItems(true)
		.MaxHeight(400.0f)
		.MultiSelect(true)
		.OnTagChanged(this, &SMessageTagContainerCombo::OnTagChanged)
		.Padding(FMargin(2,0,2,0))
		.PropertyHandle(PropertyHandle)
		.TagContainers(TagContainersToEdit);

	if (TagPicker->GetWidgetToFocusOnOpen())
	{
		ComboButton->SetMenuContentWidgetToFocus(TagPicker->GetWidgetToFocusOnOpen());
	}

	return TagPicker.ToSharedRef();
}

FReply SMessageTagContainerCombo::OnTagMenu(const FPointerEvent& MouseEvent, const FMessageTag MessageTag)
{
	FMenuBuilder MenuBuilder(/*bShouldCloseWindowAfterMenuSelection=*/ true, /*CommandList=*/ nullptr);

	TWeakPtr<SMessageTagContainerCombo> WeakSelf = StaticCastWeakPtr<SMessageTagContainerCombo>(AsWeak());

	auto IsValidTag = [MessageTag]()
	{
		return MessageTag.IsValid();		
	};
	
	MenuBuilder.AddMenuEntry(
		LOCTEXT("MessageTagContainerCombo_SearchForReferences", "Search For References"),
		FText::Format(LOCTEXT("MessageTagContainerCombo_SearchForReferencesTooltip", "Find references to the tag {0}"), FText::AsCultureInvariant(MessageTag.ToString())),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Search"),
		FUIAction(FExecuteAction::CreateLambda([MessageTag]()
			{
				// Single tag search
				const FName TagFName = MessageTag.GetTagName();
				if (FEditorDelegates::OnOpenReferenceViewer.IsBound() && !TagFName.IsNone())
				{
					TArray<FAssetIdentifier> AssetIdentifiers;
					AssetIdentifiers.Emplace(FMessageTag::StaticStruct(), TagFName);
					FEditorDelegates::OnOpenReferenceViewer.Broadcast(AssetIdentifiers, FReferenceViewerParams());
				}
			}),
			FCanExecuteAction::CreateLambda(IsValidTag))
		);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("MessageTagContainerCombo_WholeContainerSearchForReferences", "Search For Any References"),
		LOCTEXT("MessageTagContainerCombo_WholeContainerSearchForReferencesTooltip", "Find referencers that reference *any* of the tags in this container"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SMessageTagContainerCombo::OnSearchForAnyReferences), FCanExecuteAction::CreateLambda(IsValidTag)));

	MenuBuilder.AddSeparator();

	MenuBuilder.AddMenuEntry(
	NSLOCTEXT("PropertyView", "CopyProperty", "Copy"),
		FText::Format(LOCTEXT("MessageTagContainerCombo_CopyTagTooltip", "Copy tag {0} to clipboard"), FText::AsCultureInvariant(MessageTag.ToString())),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Copy"),
		FUIAction(FExecuteAction::CreateSP(this, &SMessageTagContainerCombo::OnCopyTag, MessageTag), FCanExecuteAction::CreateLambda(IsValidTag)));
	
	MenuBuilder.AddMenuEntry(
	NSLOCTEXT("PropertyView", "PasteProperty", "Paste"),
		LOCTEXT("MessageTagContainerCombo_PasteTagTooltip", "Paste tags from clipboard."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Paste"),
		FUIAction(FExecuteAction::CreateSP(this, &SMessageTagContainerCombo::OnPasteTag), FCanExecuteAction::CreateSP(this, &SMessageTagContainerCombo::CanPaste)));
	
	MenuBuilder.AddMenuEntry(
	LOCTEXT("MessageTagContainerCombo_ClearAll", "Clear All Tags"),
		FText::GetEmpty(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.X"),
		FUIAction(FExecuteAction::CreateSP(this, &SMessageTagContainerCombo::OnClearAll)));

	// Spawn context menu
	FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
	FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, MenuBuilder.MakeWidget(), MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));

	return FReply::Handled();

}

void SMessageTagContainerCombo::OnSearchForAnyReferences() const
{
	if (FEditorDelegates::OnOpenReferenceViewer.IsBound()
		&& TagsToEdit.Num() > 0)
	{
		TArray<FAssetIdentifier> AssetIdentifiers;
		AssetIdentifiers.Reserve(TagsToEdit.Num());
		for (const TSharedPtr<FEditableItem>& Item : TagsToEdit)
		{
			check(Item.IsValid());
			const FName TagFName = Item->Tag.GetTagName();
			if (!TagFName.IsNone())
			{
				AssetIdentifiers.Emplace(FMessageTag::StaticStruct(), TagFName);
			}
		}
		FEditorDelegates::OnOpenReferenceViewer.Broadcast(AssetIdentifiers, FReferenceViewerParams());
	}
}

FReply SMessageTagContainerCombo::OnEditClicked(const FMessageTag InTagToHilight)
{
	FReply Reply = FReply::Handled();
	if (ComboButton->ShouldOpenDueToClick())
	{
		TagToHilight = InTagToHilight;
		
		ComboButton->SetIsOpen(true);
		
		if (TagPicker.IsValid() && TagPicker->GetWidgetToFocusOnOpen())
		{
			Reply.SetUserFocus(TagPicker->GetWidgetToFocusOnOpen().ToSharedRef());
		}
	}
	else
	{
		ComboButton->SetIsOpen(false);
	}
	
	return Reply;
}

FReply SMessageTagContainerCombo::OnClearAllClicked()
{
	OnClearAll();
	return FReply::Handled();
}

void SMessageTagContainerCombo::OnTagChanged(const TArray<FMessageTagContainer>& TagContainers)
{
	// Property is handled in the picker.

	// Update for attribute version and callbacks.
	CachedTagContainers = TagContainers;
	
	if (!TagContainers.IsEmpty())
	{
		OnTagContainerChanged.ExecuteIfBound(TagContainers[0]);
	}

	RefreshTagContainers();
}

void SMessageTagContainerCombo::OnClearAll()
{
	if (PropertyHandle.IsValid())
	{
		FScopedTransaction Transaction(LOCTEXT("MessageTagContainerCombo_ClearAll", "Clear All Tags"));
		PropertyHandle->SetValueFromFormattedString(FMessageTagContainer().ToString());
	}

	// Update for attribute version and callbacks.
	for (FMessageTagContainer& TagContainer : CachedTagContainers)
	{
		TagContainer.Reset();
	}

	if (!CachedTagContainers.IsEmpty())
	{
		OnTagContainerChanged.ExecuteIfBound(CachedTagContainers[0]);
	}

	RefreshTagContainers();
}

void SMessageTagContainerCombo::OnCopyTag(const FMessageTag TagToCopy) const
{
	// Copy tag as a plain string, MessageTag's import text can handle that.
	FPlatformApplicationMisc::ClipboardCopy(*TagToCopy.ToString());
}

void SMessageTagContainerCombo::OnPasteTag()
{
	FString PastedText;
	FPlatformApplicationMisc::ClipboardPaste(PastedText);
	bool bHandled = false;

	// Try to paste single tag
	const FMessageTag PastedTag = UE::MessageTags::EditorUtilities::MessageTagTryImportText(PastedText);
	if (PastedTag.IsValid())
	{
		if (PropertyHandle.IsValid())
		{
			// From property
			TArray<FString> NewValues;
			SMessageTagPicker::EnumerateEditableTagContainersFromPropertyHandle(PropertyHandle.ToSharedRef(), [&NewValues, PastedTag](const FMessageTagContainer& EditableTagContainer)
			{
				FMessageTagContainer TagContainerCopy = EditableTagContainer;
				TagContainerCopy.AddTag(PastedTag);

				NewValues.Add(TagContainerCopy.ToString());
				return true;
			});

			FScopedTransaction Transaction(LOCTEXT("MessageTagContainerCombo_PasteMessageTag", "Paste Message Tag"));
			PropertyHandle->SetPerObjectValues(NewValues);
		}

		// Update for attribute version and callbacks.
		for (FMessageTagContainer& TagContainer : CachedTagContainers)
		{
			TagContainer.AddTag(PastedTag);
		}

		bHandled = true;
	}

	// Try to paste a container
	if (!bHandled)
	{
		const FMessageTagContainer PastedTagContainer = UE::MessageTags::EditorUtilities::MessageTagContainerTryImportText(PastedText);
		if (PastedTagContainer.IsValid())
		{
			if (PropertyHandle.IsValid())
			{
				// From property
				FScopedTransaction Transaction(LOCTEXT("MessageTagContainerCombo_PasteMessageTagContainer", "Paste Message Tag Container"));
				PropertyHandle->SetValueFromFormattedString(PastedText);
			}

			// Update for attribute version and callbacks.
			for (FMessageTagContainer& TagContainer : CachedTagContainers)
			{
				TagContainer = PastedTagContainer;
			}
			
			bHandled = true;
		}
	}

	if (bHandled)
	{
		if (!CachedTagContainers.IsEmpty())
		{
			OnTagContainerChanged.ExecuteIfBound(CachedTagContainers[0]);
		}

		RefreshTagContainers();
	}
}

bool SMessageTagContainerCombo::CanPaste() const
{
	FString PastedText;
	FPlatformApplicationMisc::ClipboardPaste(PastedText);
	
	const FMessageTag PastedTag = UE::MessageTags::EditorUtilities::MessageTagTryImportText(PastedText);
	if (PastedTag.IsValid())
	{
		return true;
	}

	const FMessageTagContainer PastedTagContainer = UE::MessageTags::EditorUtilities::MessageTagContainerTryImportText(PastedText);
	if (PastedTagContainer.IsValid())
	{
		return true;
	}

	return false;
}
	
FReply SMessageTagContainerCombo::OnClearTagClicked(const FMessageTag TagToClear)
{
	if (PropertyHandle.IsValid())
	{
		// From property
		TArray<FString> NewValues;
		SMessageTagPicker::EnumerateEditableTagContainersFromPropertyHandle(PropertyHandle.ToSharedRef(), [&NewValues, TagToClear](const FMessageTagContainer& EditableTagContainer)
		{
			FMessageTagContainer TagContainerCopy = EditableTagContainer;
			TagContainerCopy.RemoveTag(TagToClear);

			NewValues.Add(TagContainerCopy.ToString());
			return true;
		});

		FScopedTransaction Transaction(LOCTEXT("MessageTagContainerCombo_Remove", "Remove Message Tag"));
		PropertyHandle->SetPerObjectValues(NewValues);
	}
	
	// Update for attribute version and callbacks.
	for (FMessageTagContainer& TagContainer : CachedTagContainers)
	{
		TagContainer.RemoveTag(TagToClear);
	}

	if (!CachedTagContainers.IsEmpty())
	{
		OnTagContainerChanged.ExecuteIfBound(CachedTagContainers[0]);
	}

	RefreshTagContainers();

	return FReply::Handled();
}

void SMessageTagContainerCombo::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		RefreshTagContainers();
	}
}

void SMessageTagContainerCombo::PostRedo(bool bSuccess)
{
	if (bSuccess)
	{
		RefreshTagContainers();
	}
}

void SMessageTagContainerCombo::RefreshTagContainers()
{
	CachedTagContainers.Reset();
	TagsToEdit.Reset();

	if (PropertyHandle.IsValid())
	{
		// From property
		SMessageTagPicker::EnumerateEditableTagContainersFromPropertyHandle(PropertyHandle.ToSharedRef(), [this](const FMessageTagContainer& InTagContainer)
		{
			CachedTagContainers.Add(InTagContainer);

			for (auto It = InTagContainer.CreateConstIterator(); It; ++It)
			{
				const FMessageTag Tag = *It;
				const int32 ExistingItemIndex = TagsToEdit.IndexOfByPredicate([Tag](const TSharedPtr<FEditableItem>& Item)
				{
					return Item.IsValid() && Item->Tag == Tag;
				});
				if (ExistingItemIndex != INDEX_NONE)
				{
					TagsToEdit[ExistingItemIndex]->Count++;
				}
				else
				{
					TagsToEdit.Add(MakeShared<FEditableItem>(Tag));
				}
			}
			
			return true;
		});
	}
	else
	{
		// From attribute
		const FMessageTagContainer& InTagContainer = TagContainerAttribute.Get(); 
		
		CachedTagContainers.Add(InTagContainer);

		for (auto It = InTagContainer.CreateConstIterator(); It; ++It)
		{
			TagsToEdit.Add(MakeShared<FEditableItem>(*It));
		}
	}

	const int32 PropertyCount = CachedTagContainers.Num();
	for (TSharedPtr<FEditableItem>& Item : TagsToEdit)
	{
		check(Item.IsValid());
		if (Item->Count != PropertyCount)
		{
			Item->bMultipleValues = true;
		}
	}
	
	TagsToEdit.StableSort([](const TSharedPtr<FEditableItem>& LHS, const TSharedPtr<FEditableItem>& RHS)
	{
		check(LHS.IsValid() && RHS.IsValid());
		return LHS->Tag < RHS->Tag;
	});

	// Refresh the slate list
	if (TagListView.IsValid())
	{
#if UE_5_02_OR_LATER
		TagListView->SetItemsSource(&TagsToEdit);
#else
		struct FListViewFriend : public SListView<TSharedPtr<FEditableItem>>
		{
			using SListView<TSharedPtr<FEditableItem>>::ItemsSource;
		};
		static_cast<FListViewFriend*>(TagListView.Get())->ItemsSource = &TagsToEdit;
#endif
		TagListView->RequestListRefresh();
	}
}

#undef LOCTEXT_NAMESPACE
#endif
