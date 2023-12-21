// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMessageTagPicker.h"
#include "Misc/ConfigCacheIni.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Styling/AppStyle.h"
#include "Widgets/SWindow.h"
#include "Misc/MessageDialog.h"
#include "MessageTagsModule.h"
#include "ScopedTransaction.h"
#include "Textures/SlateIcon.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SSearchBox.h"
#include "MessageTagsEditorModule.h"
#include "Framework/Application/SlateApplication.h"
#include "SAddNewMessageTagWidget.h"
#include "SAddNewRestrictedMessageTagWidget.h"
#include "SRenameMessageTagDialog.h"
#include "AssetRegistry/AssetData.h"
#include "Editor.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/EnumerateRange.h"
#include "Styling/StyleColors.h"

#define LOCTEXT_NAMESPACE "MessageTagPicker"

const FString SMessageTagPicker::SettingsIniSection = TEXT("MessageTagPicker");

bool SMessageTagPicker::EnumerateEditableTagContainersFromPropertyHandle(const TSharedRef<IPropertyHandle>& PropHandle, TFunctionRef<bool(const FMessageTagContainer&)> Callback)
{
	const FStructProperty* StructProperty = CastField<FStructProperty>(PropHandle->GetProperty());
	if (StructProperty && StructProperty->Struct->IsChildOf(FMessageTagContainer::StaticStruct()))
	{
		PropHandle->EnumerateRawData([&Callback](void* RawData, const int32 /*DataIndex*/, const int32 /*NumDatas*/)
		{
			// Report empty container even if the instance data is null to match all the instances indices of the property handle.
			if (RawData)
			{
				return Callback(*static_cast<FMessageTagContainer*>(RawData));
			}
			return Callback(FMessageTagContainer());
		});
		return true;
	}
	if (StructProperty && StructProperty->Struct->IsChildOf(FMessageTag::StaticStruct()))
	{
		PropHandle->EnumerateRawData([&Callback](void* RawData, const int32 /*DataIndex*/, const int32 /*NumDatas*/)
		{
			// Report empty container even if the instance data is null to match all the instances indices of the property handle.
			FMessageTagContainer Container;
			if (RawData)
			{
				Container.AddTag(*static_cast<FMessageTag*>(RawData));
			}
			return Callback(Container);
		});
		return true;
	}
	return false;
}

bool SMessageTagPicker::GetEditableTagContainersFromPropertyHandle(const TSharedRef<IPropertyHandle>& PropHandle, TArray<FMessageTagContainer>& OutEditableTagContainers)
{
	OutEditableTagContainers.Reset();
	return EnumerateEditableTagContainersFromPropertyHandle(PropHandle, [&OutEditableTagContainers](const FMessageTagContainer& EditableTagContainer)
	{
		OutEditableTagContainers.Add(EditableTagContainer);
		return true;
	});
}

void SMessageTagPicker::Construct(const FArguments& InArgs)
{
	TagContainers = InArgs._TagContainers;
	if (InArgs._PropertyHandle.IsValid())
	{
		// If we're backed by a property handle then try and get the tag containers from the property handle
		GetEditableTagContainersFromPropertyHandle(InArgs._PropertyHandle.ToSharedRef(), TagContainers);
	}

	// If we're in management mode, we don't need to have editable tag containers.
	ensure(TagContainers.Num() > 0 || InArgs._MessageTagPickerMode == EMessageTagPickerMode::ManagementMode);

	OnTagChanged = InArgs._OnTagChanged;
	OnRefreshTagContainers = InArgs._OnRefreshTagContainers;
	bReadOnly = InArgs._ReadOnly;
	SettingsName = InArgs._SettingsName;
	bMultiSelect = InArgs._MultiSelect;
	PropertyHandle = InArgs._PropertyHandle;
	RootFilterString = InArgs._Filter;
	MessageTagPickerMode = InArgs._MessageTagPickerMode;

	bDelayRefresh = false;
	MaxHeight = InArgs._MaxHeight;

	bRestrictedTags = InArgs._RestrictedTags;

	UMessageTagsManager::OnEditorRefreshMessageTagTree.AddSP(this, &SMessageTagPicker::RefreshOnNextTick);
	UMessageTagsManager& Manager = UMessageTagsManager::Get();

	GetFilteredMessageRootTags(RootFilterString, TagItems);

	if (bRestrictedTags)
	{
		// We only want to show the restricted Message tags
		for (int32 Idx = TagItems.Num() - 1; Idx >= 0; --Idx)
		{
			if (!TagItems[Idx]->IsRestrictedMessageTag())
			{
				TagItems.RemoveAtSwap(Idx);
			}
		}
	}

	if (Manager.OnFilterMessageTag.IsBound())
	{
		for (int32 Idx = TagItems.Num() - 1; Idx >= 0; --Idx)
		{
			bool DelegateShouldHide = false;
			FMessageTagSource* Source = Manager.FindTagSource(TagItems[Idx]->GetFirstSourceName());
			Manager.OnFilterMessageTag.Broadcast(UMessageTagsManager::FFilterMessageTagContext(RootFilterString, TagItems[Idx], Source, PropertyHandle), DelegateShouldHide);
			if (DelegateShouldHide)
			{
				TagItems.RemoveAtSwap(Idx);
			}
		}
	}

	const FText NewTagText = bRestrictedTags ? LOCTEXT("AddNewRestrictedTag", "Add New Restricted Message Tag") : LOCTEXT("AddNewTag", "Add New Message Tag");
	
	TSharedPtr<SComboButton> SettingsCombo = SNew(SComboButton)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.HasDownArrow(false)
		.ButtonContent()
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush("Icons.Settings"))
			.ColorAndOpacity(FSlateColor::UseForeground())
		];
	SettingsCombo->SetOnGetMenuContent(FOnGetContent::CreateSP(this, &SMessageTagPicker::MakeSettingsMenu, SettingsCombo));


	TWeakPtr<SMessageTagPicker> WeakSelf = StaticCastWeakPtr<SMessageTagPicker>(AsWeak());
	
	TSharedRef<SWidget> Picker = 
		SNew(SBorder)
		.Padding(InArgs._Padding)
		.BorderImage(FStyleDefaults::GetNoBrush())
		[
			SNew(SVerticalBox)

			// Message Tag Tree controls
			+SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Top)
			[
				SNew(SHorizontalBox)

				// Smaller add button for selection and hybrid modes.
				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SHorizontalBox)

					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(FMargin(0,0,4,0))
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
						.ToolTipText_Lambda([WeakSelf]() -> FText
						{
							const TSharedPtr<SMessageTagPicker> Self = WeakSelf.Pin();
							if (Self.IsValid() && Self->bNewTagWidgetVisible)
							{
								return LOCTEXT("CloseSection", "Close Section");
							}
							return LOCTEXT("AddNewMessageTag", "Add New Message Tag");
						})
						.OnClicked_Lambda([WeakSelf]()
						{
							if (const TSharedPtr<SMessageTagPicker> Self = WeakSelf.Pin())
							{
								if (!Self->bNewTagWidgetVisible)
								{
									// If we have a selected item, by default add child, else new root tag. 
									TArray<TSharedPtr<FMessageTagNode>> Selection = Self->TagTreeWidget->GetSelectedItems();
									if (Selection.Num() > 0 && Selection[0].IsValid())
									{
										Self->ShowInlineAddTagWidget(EMessageTagAdd::Child, Selection[0]);
									}
									else
									{
										Self->ShowInlineAddTagWidget(EMessageTagAdd::Root);
									}
								}
								else
								{
									Self->bNewTagWidgetVisible = false;
								}
							}
							return FReply::Handled();
						})
						[
							SNew(SImage)
							.Image(FAppStyle::GetBrush("Icons.Plus"))
							.ColorAndOpacity(FStyleColors::AccentGreen)
						]
					]
				]

				// Search
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.FillWidth(1.f)
				.Padding(0,1,5,1)
				[
					SAssignNew(SearchTagBox, SSearchBox)
					.HintText(LOCTEXT("MessageTagPicker_SearchBoxHint", "Search Message Tags"))
					.OnTextChanged(this, &SMessageTagPicker::OnFilterTextChanged)
				]

				// View settings
				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SettingsCombo.ToSharedRef()
				]
			]

			// Inline add new tag window for selection and hybrid modes.
			+SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Top)
			.Padding(FMargin(4,2))
			[
				SAssignNew(AddNewTagWidget, SAddNewMessageTagWidget )
				.Padding(FMargin(0, 2))
				.AddButtonPadding(FMargin(0, 4, 0, 0))
				.Visibility_Lambda([WeakSelf]()
				{
					const TSharedPtr<SMessageTagPicker> Self = WeakSelf.Pin();
					return (Self.IsValid() && Self->bNewTagWidgetVisible) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				.OnMessageTagAdded_Lambda([WeakSelf](const FString& TagName, const FString& TagComment, const FName& TagSource)
				{
				   if (const TSharedPtr<SMessageTagPicker> Self = WeakSelf.Pin())
				   {
					   Self->OnMessageTagAdded(TagName, TagComment, TagSource);
				   }
				})
			]

			// Message Tags tree
			+SVerticalBox::Slot()
			.MaxHeight(MaxHeight)
			.FillHeight(1)
			[
				SAssignNew(TagTreeContainerWidget, SBorder)
				.BorderImage(FStyleDefaults::GetNoBrush())
				[
					SAssignNew(TagTreeWidget, STreeView<TSharedPtr<FMessageTagNode>>)
					.TreeItemsSource(&TagItems)
					.OnGenerateRow(this, &SMessageTagPicker::OnGenerateRow)
					.OnGetChildren(this, &SMessageTagPicker::OnGetChildren)
					.OnExpansionChanged(this, &SMessageTagPicker::OnExpansionChanged)
					.SelectionMode(ESelectionMode::Single)
					.OnContextMenuOpening(this, &SMessageTagPicker::OnTreeContextMenuOpening)
					.OnSelectionChanged(this, &SMessageTagPicker::OnTreeSelectionChanged)
#if UE_5_03_OR_LATER
					.OnKeyDownHandler(this, &SMessageTagPicker::OnTreeKeyDown)
#endif
				]
			]
		];

	if (InArgs._ShowMenuItems)
	{
		FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection*/false, nullptr);

		if (InArgs._MultiSelect)
		{
			MenuBuilder.BeginSection(FName(), LOCTEXT("SectionMessageTagContainer", "MessageTag Container"));
		}
		else
		{
			MenuBuilder.BeginSection(FName(), LOCTEXT("SectionMessageTag", "MessageTag"));
		}
		
		MenuBuilder.AddMenuEntry(
			LOCTEXT("MessageTagPicker_ClearSelection", "Clear Selection"), FText::GetEmpty(), FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.X"),
			FUIAction(FExecuteAction::CreateRaw(this, &SMessageTagPicker::OnClearAllClicked, TSharedPtr<SComboButton>()))
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("MessageTagPicker_ManageTags", "Manage Message Tags..."), FText::GetEmpty(), FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"),
			FUIAction(FExecuteAction::CreateRaw(this, &SMessageTagPicker::OnManageTagsClicked, TSharedPtr<FMessageTagNode>(), TSharedPtr<SComboButton>()))
		);

		MenuBuilder.AddSeparator();

		TSharedRef<SWidget> MenuContent =
			SNew(SBox)
			.WidthOverride(300.0f)
			.HeightOverride(MaxHeight)
			[
				Picker
			];
		MenuBuilder.AddWidget(MenuContent, FText::GetEmpty(), true);

		MenuBuilder.EndSection();
		
		ChildSlot
		[
			MenuBuilder.MakeWidget()
		];
	}
	else
	{
		ChildSlot
		[
			Picker
		];
	}
	
	// Force the entire tree collapsed to start
	SetTagTreeItemExpansion(/*bExpand*/false, /*bPersistExpansion*/false);

	LoadSettings();

	VerifyAssetTagValidity();
}

TSharedPtr<SWidget> SMessageTagPicker::OnTreeContextMenuOpening()
{
	TArray<TSharedPtr<FMessageTagNode>> Selection = TagTreeWidget->GetSelectedItems();
	const TSharedPtr<FMessageTagNode> SelectedTagNode = Selection.IsEmpty() ? nullptr : Selection[0];
	return MakeTagActionsMenu(SelectedTagNode, TSharedPtr<SComboButton>(), /*bInShouldCloseWindowAfterMenuSelection*/true);
}

void SMessageTagPicker::OnTreeSelectionChanged(TSharedPtr<FMessageTagNode> SelectedItem, ESelectInfo::Type SelectInfo)
{
	if (MessageTagPickerMode == EMessageTagPickerMode::SelectionMode
		&& SelectInfo == ESelectInfo::OnMouseClick)
	{
		// In selection mode we do not allow to select lines as they have not meaning,
		// but the highlight helps navigating the list.   
		if (!bInSelectionChanged)
		{
			TGuardValue<bool> PersistExpansionChangeGuard(bInSelectionChanged, true);
			TagTreeWidget->ClearSelection();

			// Toggle selection
			const ECheckBoxState State = IsTagChecked(SelectedItem);
			if (State == ECheckBoxState::Unchecked)
			{
				OnTagChecked(SelectedItem);
			}
			else if (State == ECheckBoxState::Checked)
			{
				OnTagUnchecked(SelectedItem);
			}
		}
	}
}

FReply SMessageTagPicker::OnTreeKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (FSlateApplication::Get().GetNavigationActionFromKey(InKeyEvent) == EUINavigationAction::Accept)
	{
		TArray<TSharedPtr<FMessageTagNode>> Selection = TagTreeWidget->GetSelectedItems();
		TSharedPtr<FMessageTagNode> SelectedItem;
		if (!Selection.IsEmpty())
		{
			SelectedItem = Selection[0];
		}

		if (SelectedItem.IsValid())
		{
			TGuardValue<bool> PersistExpansionChangeGuard(bInSelectionChanged, true);

			// Toggle selection
			const ECheckBoxState State = IsTagChecked(SelectedItem);
			if (State == ECheckBoxState::Unchecked)
			{
				OnTagChecked(SelectedItem);
			}
			else if (State == ECheckBoxState::Checked)
			{
				OnTagUnchecked(SelectedItem);
			}
			
			return FReply::Handled();
		}
	}

	return SCompoundWidget::OnKeyDown(InGeometry, InKeyEvent);
}

TSharedRef<SWidget> SMessageTagPicker::MakeSettingsMenu(TSharedPtr<SComboButton> OwnerCombo)
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection*/false, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("MessageTagPicker_ExpandAll", "Expand All"), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &SMessageTagPicker::OnExpandAllClicked, OwnerCombo))
	);
	
	MenuBuilder.AddMenuEntry(
		LOCTEXT("MessageTagPicker_CollapseAll", "Collapse All"), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &SMessageTagPicker::OnCollapseAllClicked, OwnerCombo))
	);
	
	return MenuBuilder.MakeWidget();
}

void SMessageTagPicker::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (PropertyHandle.IsValid())
	{
		// If we're backed by a property handle then try and refresh the tag containers, 
		// as they may have changed under us (eg, from object re-instancing)
		GetEditableTagContainersFromPropertyHandle(PropertyHandle.ToSharedRef(), TagContainers);
	}

	if (bDelayRefresh)
	{
		RefreshTags();
		bDelayRefresh = false;
	}

	if (RequestedScrollToTag.IsValid())
	{
		// Scroll specified item into view.
		UMessageTagsManager& Manager = UMessageTagsManager::Get();
		TSharedPtr<FMessageTagNode> Node = Manager.FindTagNode(RequestedScrollToTag);

		if (Node.IsValid())
		{
			// Expand all the parent nodes to make sure the target node is visible.
			TSharedPtr<FMessageTagNode> ParentNode = Node.IsValid() ? Node->ParentNode : nullptr;
			while (ParentNode.IsValid())
			{
				TagTreeWidget->SetItemExpansion(ParentNode, /*bExpand*/true);
				ParentNode = ParentNode->ParentNode;
			}

			TagTreeWidget->ClearSelection();
			TagTreeWidget->SetItemSelection(Node, true);
			TagTreeWidget->RequestScrollIntoView(Node);
		}
		
		RequestedScrollToTag = FMessageTag();
	}
}

void SMessageTagPicker::OnFilterTextChanged(const FText& InFilterText)
{
	FilterString = InFilterText.ToString();	
	FilterTagTree();
}

void SMessageTagPicker::FilterTagTree()
{
	if (FilterString.IsEmpty())
	{
		TagTreeWidget->SetTreeItemsSource(&TagItems);

		for (int32 iItem = 0; iItem < TagItems.Num(); ++iItem)
		{
			SetDefaultTagNodeItemExpansion(TagItems[iItem]);
		}
	}
	else
	{
		FilteredTagItems.Empty();

		for (int32 iItem = 0; iItem < TagItems.Num(); ++iItem)
		{
			if (FilterChildrenCheck(TagItems[iItem]))
			{
				FilteredTagItems.Add(TagItems[iItem]);
				SetTagNodeItemExpansion(TagItems[iItem], true);
			}
			else
			{
				SetTagNodeItemExpansion(TagItems[iItem], false);
			}
		}

		TagTreeWidget->SetTreeItemsSource(&FilteredTagItems);
	}

	TagTreeWidget->RequestTreeRefresh();
}

bool SMessageTagPicker::FilterChildrenCheckRecursive(TSharedPtr<FMessageTagNode>& InItem) const
{
	for (TSharedPtr<FMessageTagNode>& Child : InItem->GetChildTagNodes())
	{
		if (FilterChildrenCheck(Child))
		{
			return true;
		}
	}
	return false;
}

bool SMessageTagPicker::FilterChildrenCheck(TSharedPtr<FMessageTagNode>& InItem) const
{
	if (!InItem.IsValid())
	{
		return false;
	}

	if (bRestrictedTags && !InItem->IsRestrictedMessageTag())
	{
		return false;
	}

	UMessageTagsManager& Manager = UMessageTagsManager::Get();
	bool bDelegateShouldHide = false;
	Manager.OnFilterMessageTagChildren.Broadcast(RootFilterString, InItem, bDelegateShouldHide);
	if (!bDelegateShouldHide && Manager.OnFilterMessageTag.IsBound())
	{
		FMessageTagSource* Source = Manager.FindTagSource(InItem->GetFirstSourceName());
		Manager.OnFilterMessageTag.Broadcast(UMessageTagsManager::FFilterMessageTagContext(RootFilterString, InItem, Source, PropertyHandle), bDelegateShouldHide);
	}
	if (bDelegateShouldHide)
	{
		// The delegate wants to hide, see if any children need to show
		return FilterChildrenCheckRecursive(InItem);
	}

	if (InItem->GetCompleteTagString().Contains(FilterString) || FilterString.IsEmpty())
	{
		return true;
	}

	return FilterChildrenCheckRecursive(InItem);
}

FText SMessageTagPicker::GetHighlightText() const
{
	return FilterString.IsEmpty() ? FText::GetEmpty() : FText::FromString(FilterString);
}

TSharedRef<ITableRow> SMessageTagPicker::OnGenerateRow(TSharedPtr<FMessageTagNode> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	FText TooltipText;
	FString TagSource;
	bool bIsExplicitTag = true;
	if (InItem.IsValid())
	{
		UMessageTagsManager& Manager = UMessageTagsManager::Get();

		FName TagName = InItem.Get()->GetCompleteTagName();
		TSharedPtr<FMessageTagNode> Node = Manager.FindTagNode(TagName);

		FString TooltipString = TagName.ToString();

		if (Node.IsValid())
		{
			constexpr int32 MaxSourcesToDisplay = 3; // How many sources to display before showing ellipsis (tool tip will have all sources). 

			FString AllSources;
			for (TConstEnumerateRef<FName> Source : EnumerateRange(Node->GetAllSourceNames()))
			{
				if (AllSources.Len() > 0)
				{
					AllSources += TEXT(", ");
				}
				AllSources += Source->ToString();
				
				if (Source.GetIndex() < MaxSourcesToDisplay)
				{
					if (TagSource.Len() > 0)
					{
						TagSource += TEXT(", ");
					}
					TagSource += Source->ToString();
				}
			}
			
			if (Node->GetAllSourceNames().Num() > MaxSourcesToDisplay)
			{
				TagSource += FString::Printf(TEXT(", ... (%d)"), Node->GetAllSourceNames().Num() - MaxSourcesToDisplay);
			}

			bIsExplicitTag = Node->bIsExplicitTag;

			TooltipString.Append(FString::Printf(TEXT("\n(%s%s)"), bIsExplicitTag ? TEXT("") : TEXT("Implicit "), *AllSources));

			// tag comments
			if (!Node->DevComment.IsEmpty())
			{
				TooltipString.Append(FString::Printf(TEXT("\n\n%s"), *Node->DevComment));
			}

			// info related to conflicts
			if (Node->bDescendantHasConflict)
			{
				TooltipString.Append(TEXT("\n\nA tag that descends from this tag has a source conflict."));
			}

			if (Node->bAncestorHasConflict)
			{
				TooltipString.Append(TEXT("\n\nThis tag is descended from a tag that has a conflict. No operations can be performed on this tag until the conflict is resolved."));
			}

			if (Node->bNodeHasConflict)
			{
				TooltipString.Append(TEXT("\n\nThis tag comes from multiple sources. Tags may only have one source."));
			}
		}

		TooltipText = FText::FromString(TooltipString);
	}

	TSharedPtr<SComboButton> ActionsCombo = SNew(SComboButton)
		.ToolTipText(LOCTEXT("MoreActions", "More Actions..."))
		.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
		.ContentPadding(0)
		.ForegroundColor(FSlateColor::UseForeground())
		.HasDownArrow(true)
		.CollapseMenuOnParentFocus(true);

	// Craete context menu with bInShouldCloseWindowAfterMenuSelection = false, or else the actions menu action will not work due the popup-menu handling order.
	ActionsCombo->SetOnGetMenuContent(FOnGetContent::CreateSP(this, &SMessageTagPicker::MakeTagActionsMenu, InItem, ActionsCombo, /*bInShouldCloseWindowAfterMenuSelection*/false));
	
	if (MessageTagPickerMode == EMessageTagPickerMode::SelectionMode
		|| MessageTagPickerMode == EMessageTagPickerMode::HybridMode)
	{
		return SNew(STableRow<TSharedPtr<FMessageTagNode>>, OwnerTable)
#if UE_5_03_OR_LATER
		.Style(FAppStyle::Get(), "MessageTagTreeView")
#endif
		.ToolTipText(TooltipText)
		[
			SNew(SHorizontalBox)

			// Tag Selection (selection mode only)
			+SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.OnCheckStateChanged(this, &SMessageTagPicker::OnTagCheckStatusChanged, InItem)
				.IsChecked(this, &SMessageTagPicker::IsTagChecked, InItem)
				.IsEnabled(this, &SMessageTagPicker::CanSelectTags)
				.CheckBoxContentUsesAutoWidth(false)
				[
					SNew(STextBlock)
					.HighlightText(this, &SMessageTagPicker::GetHighlightText)
					.Text(FText::FromName(InItem->GetSimpleTagName()))
				]
			]

			// Allows non-restricted children checkbox
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("AllowsChildren", "Does this restricted tag allow non-restricted children"))
				.OnCheckStateChanged(this, &SMessageTagPicker::OnAllowChildrenTagCheckStatusChanged, InItem)
				.IsChecked(this, &SMessageTagPicker::IsAllowChildrenTagChecked, InItem)
				.Visibility(this, &SMessageTagPicker::DetermineAllowChildrenVisible, InItem)
			]

			// More Actions Menu
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				ActionsCombo.ToSharedRef()
			]
		];
	}
	else
	{
		return SNew(STableRow<TSharedPtr<FMessageTagNode>>, OwnerTable)
#if UE_5_03_OR_LATER
			.Style(FAppStyle::Get(), "MessageTagTreeView")
#endif
		[
			SNew(SHorizontalBox)

			// Normal Tag Display (management mode only)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.ToolTip(FSlateApplication::Get().MakeToolTip(TooltipText))
				.Text(FText::FromName(InItem->GetSimpleTagName()))
				.ColorAndOpacity(this, &SMessageTagPicker::GetTagTextColour, InItem)
				.HighlightText(this, &SMessageTagPicker::GetHighlightText)
			]

			// Source
			+SHorizontalBox::Slot()
			.FillWidth(1)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.Padding(FMargin(16,0, 4, 0))
			[
				SNew(STextBlock)
				.Clipping(EWidgetClipping::OnDemand)
				.ToolTip(FSlateApplication::Get().MakeToolTip(TooltipText))
				.Text(FText::FromString(TagSource) )
				.ColorAndOpacity(bIsExplicitTag ? FLinearColor(1,1,1,0.5f) : FLinearColor(1,1,1,0.25f))
			]

			// Allows non-restricted children checkbox
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("AllowsChildren", "Does this restricted tag allow non-restricted children"))
				.OnCheckStateChanged(this, &SMessageTagPicker::OnAllowChildrenTagCheckStatusChanged, InItem)
				.IsChecked(this, &SMessageTagPicker::IsAllowChildrenTagChecked, InItem)
				.Visibility(this, &SMessageTagPicker::DetermineAllowChildrenVisible, InItem)
			]

			// More Actions Menu
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				ActionsCombo.ToSharedRef()
			]
		];
	}
}

void SMessageTagPicker::OnGetChildren(TSharedPtr<FMessageTagNode> InItem, TArray<TSharedPtr<FMessageTagNode>>& OutChildren)
{
	TArray<TSharedPtr<FMessageTagNode>> FilteredChildren;
	TArray<TSharedPtr<FMessageTagNode>> Children = InItem->GetChildTagNodes();

	for (int32 iChild = 0; iChild < Children.Num(); ++iChild)
	{
		if (FilterChildrenCheck(Children[iChild]))
		{
			FilteredChildren.Add(Children[iChild]);
		}
	}
	OutChildren += FilteredChildren;
}

void SMessageTagPicker::OnTagCheckStatusChanged(ECheckBoxState NewCheckState, TSharedPtr<FMessageTagNode> NodeChanged)
{
	if (NewCheckState == ECheckBoxState::Checked)
	{
		OnTagChecked(NodeChanged);
	}
	else if (NewCheckState == ECheckBoxState::Unchecked)
	{
		OnTagUnchecked(NodeChanged);
	}
}

void SMessageTagPicker::OnTagChecked(TSharedPtr<FMessageTagNode> NodeChecked)
{
	FScopedTransaction Transaction(LOCTEXT("MessageTagPicker_SelectTags", "Select Message Tags"));

	UMessageTagsManager& TagsManager = UMessageTagsManager::Get();

	for (FMessageTagContainer& Container : TagContainers)
	{
		TSharedPtr<FMessageTagNode> CurNode(NodeChecked);

		bool bRemoveParents = false;

		while (CurNode.IsValid())
		{
			FMessageTag MessageTag = CurNode->GetCompleteTag();

			if (bRemoveParents == false)
			{
				bRemoveParents = true;
				if (bMultiSelect == false)
				{
					Container.Reset();
				}
				Container.AddTag(MessageTag);
			}
			else
			{
				Container.RemoveTag(MessageTag);
			}

			CurNode = CurNode->GetParentTagNode();
		}
	}

	OnContainersChanged();
}

void SMessageTagPicker::OnTagUnchecked(TSharedPtr<FMessageTagNode> NodeUnchecked)
{
	FScopedTransaction Transaction(LOCTEXT("MessageTagPicker_RemoveTags", "Remove Message Tags"));
	if (NodeUnchecked.IsValid())
	{
		UMessageTagsManager& TagsManager = UMessageTagsManager::Get();

		for (FMessageTagContainer& Container : TagContainers)
		{
			FMessageTag MessageTag = NodeUnchecked->GetCompleteTag();

			Container.RemoveTag(MessageTag);

			TSharedPtr<FMessageTagNode> ParentNode = NodeUnchecked->GetParentTagNode();
			if (ParentNode.IsValid())
			{
				// Check if there are other siblings before adding parent
				bool bOtherSiblings = false;
				for (auto It = ParentNode->GetChildTagNodes().CreateConstIterator(); It; ++It)
				{
					MessageTag = It->Get()->GetCompleteTag();
					if (Container.HasTagExact(MessageTag))
					{
						bOtherSiblings = true;
						break;
					}
				}
				// Add Parent
				if (!bOtherSiblings)
				{
					MessageTag = ParentNode->GetCompleteTag();
					Container.AddTag(MessageTag);
				}
			}

			// Uncheck Children
			for (const auto& ChildNode : NodeUnchecked->GetChildTagNodes())
			{
				UncheckChildren(ChildNode, Container);
			}
		}
		
		OnContainersChanged();
	}
}

void SMessageTagPicker::UncheckChildren(TSharedPtr<FMessageTagNode> NodeUnchecked, FMessageTagContainer& EditableContainer)
{
	UMessageTagsManager& TagsManager = UMessageTagsManager::Get();

	FMessageTag MessageTag = NodeUnchecked->GetCompleteTag();
	EditableContainer.RemoveTag(MessageTag);

	// Uncheck Children
	for (const auto& ChildNode : NodeUnchecked->GetChildTagNodes())
	{
		UncheckChildren(ChildNode, EditableContainer);
	}
}

ECheckBoxState SMessageTagPicker::IsTagChecked(TSharedPtr<FMessageTagNode> Node) const
{
	int32 NumValidAssets = 0;
	int32 NumAssetsTagIsAppliedTo = 0;

	if (Node.IsValid())
	{
		UMessageTagsManager& TagsManager = UMessageTagsManager::Get();

		for (const FMessageTagContainer& Container : TagContainers)
		{
			NumValidAssets++;
			const FMessageTag MessageTag = Node->GetCompleteTag();
			if (MessageTag.IsValid())
			{
				if (Container.HasTag(MessageTag))
				{
					++NumAssetsTagIsAppliedTo;
				}
			}
		}
	}

	if (NumAssetsTagIsAppliedTo == 0)
	{
		return ECheckBoxState::Unchecked;
	}
	else if (NumAssetsTagIsAppliedTo == NumValidAssets)
	{
		return ECheckBoxState::Checked;
	}
	else
	{
		return ECheckBoxState::Undetermined;
	}
}

bool SMessageTagPicker::IsExactTagInCollection(TSharedPtr<FMessageTagNode> Node) const
{
	if (Node.IsValid())
	{
		for (const FMessageTagContainer& Container : TagContainers)
		{
			FMessageTag MessageTag = Node->GetCompleteTag();
			if (MessageTag.IsValid())
			{
				if (Container.HasTagExact(MessageTag))
				{
					return true;
				}
			}
		}
	}

	return false;
}

void SMessageTagPicker::OnAllowChildrenTagCheckStatusChanged(ECheckBoxState NewCheckState, TSharedPtr<FMessageTagNode> NodeChanged)
{
	IMessageTagsEditorModule& TagsEditor = IMessageTagsEditorModule::Get();

	if (TagsEditor.UpdateTagInINI(NodeChanged->GetCompleteTagString(), NodeChanged->DevComment, NodeChanged->bIsRestrictedTag, NewCheckState == ECheckBoxState::Checked))
	{
		if (NewCheckState == ECheckBoxState::Checked)
		{
			NodeChanged->bAllowNonRestrictedChildren = true;
		}
		else if (NewCheckState == ECheckBoxState::Unchecked)
		{
			NodeChanged->bAllowNonRestrictedChildren = false;
		}
	}
}

ECheckBoxState SMessageTagPicker::IsAllowChildrenTagChecked(TSharedPtr<FMessageTagNode> Node) const
{
	if (Node->GetAllowNonRestrictedChildren())
	{
		return ECheckBoxState::Checked;
	}

	return ECheckBoxState::Unchecked;
}

EVisibility SMessageTagPicker::DetermineAllowChildrenVisible(TSharedPtr<FMessageTagNode> Node) const
{
	// We do not allow you to modify nodes that have a conflict or inherit from a node with a conflict
	if (Node->bNodeHasConflict || Node->bAncestorHasConflict)
	{
		return EVisibility::Hidden;
	}

	if (bRestrictedTags)
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}

void SMessageTagPicker::OnManageTagsClicked(TSharedPtr<FMessageTagNode> Node, TSharedPtr<SComboButton> OwnerCombo)
{
	FMessageTagManagerWindowArgs Args;
	Args.bRestrictedTags = bRestrictedTags;
	Args.Filter = RootFilterString;
	if (Node.IsValid())
	{
		Args.HighlightedTag = Node->GetCompleteTag();
	}
	
	UE::MessageTags::Editor::OpenMessageTagManager(Args);

	if (OwnerCombo.IsValid())
	{
		OwnerCombo->SetIsOpen(false);
	}
}

void SMessageTagPicker::OnClearAllClicked(TSharedPtr<SComboButton> OwnerCombo)
{
	FScopedTransaction Transaction(LOCTEXT("MessageTagPicker_RemoveAllTags", "Remove All Message Tags") );

	for (FMessageTagContainer& Container : TagContainers)
	{
		Container.Reset();
	}

	OnContainersChanged();

	if (OwnerCombo.IsValid())
	{
		OwnerCombo->SetIsOpen(false);
	}
}

FSlateColor SMessageTagPicker::GetTagTextColour(TSharedPtr<FMessageTagNode> Node) const
{
	static const FLinearColor DefaultTextColour = FLinearColor::White;
	static const FLinearColor DescendantConflictTextColour = FLinearColor(1.f, 0.65f, 0.f); // orange
	static const FLinearColor NodeConflictTextColour = FLinearColor::Red;
	static const FLinearColor AncestorConflictTextColour = FLinearColor(1.f, 1.f, 1.f, 0.5f);

	if (Node->bNodeHasConflict)
	{
		return NodeConflictTextColour;
	}

	if (Node->bDescendantHasConflict)
	{
		return DescendantConflictTextColour;
	}

	if (Node->bAncestorHasConflict)
	{
		return AncestorConflictTextColour;
	}

	return DefaultTextColour;
}

void SMessageTagPicker::OnExpandAllClicked(TSharedPtr<SComboButton> OwnerCombo)
{
	SetTagTreeItemExpansion(/*bExpand*/true, /*bPersistExpansion*/true);
	if (OwnerCombo.IsValid())
	{
		OwnerCombo->SetIsOpen(false);
	}
}

void SMessageTagPicker::OnCollapseAllClicked(TSharedPtr<SComboButton> OwnerCombo)
{
	SetTagTreeItemExpansion(/*bExpand*/false, /*bPersistExpansion*/true);
	if (OwnerCombo.IsValid())
	{
		OwnerCombo->SetIsOpen(false);
	}
}

void SMessageTagPicker::OpenAddTagDialog(const EMessageTagAdd Mode, TSharedPtr<FMessageTagNode> InTagNode)
{
	TSharedPtr<SWindow> NewTagWindow = SNew(SWindow)
		.Title(LOCTEXT("EditTagWindowTitle", "Edit Message Tag"))
		.SizingRule(ESizingRule::Autosized)
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	UMessageTagsManager& Manager = UMessageTagsManager::Get();
	
	FString TagName;
	FName TagFName;
	FString TagComment;
	FName TagSource;
	bool bTagIsExplicit;
	bool bTagIsRestricted;
	bool bTagAllowsNonRestrictedChildren;

	if (InTagNode.IsValid())
	{
		TagName = InTagNode->GetCompleteTagString();
		TagFName = InTagNode->GetCompleteTagName();
	}

	Manager.GetTagEditorData(TagFName, TagComment, TagSource, bTagIsExplicit, bTagIsRestricted, bTagAllowsNonRestrictedChildren);

	TWeakPtr<SMessageTagPicker> WeakSelf = StaticCastWeakPtr<SMessageTagPicker>(AsWeak());
	
	if (!bRestrictedTags)
    {
		TSharedRef<SAddNewMessageTagWidget> AddNewTagDialog = SNew(SAddNewMessageTagWidget)
			.OnMessageTagAdded_Lambda([WeakSelf, NewTagWindow](const FString& TagName, const FString& TagComment, const FName& TagSource)
			{
				if (const TSharedPtr<SMessageTagPicker> Self = WeakSelf.Pin())
				{
					Self->OnMessageTagAdded(TagName, TagComment, TagSource);
				}
				if (NewTagWindow.IsValid())
				{
					NewTagWindow->RequestDestroyWindow();
				}
			});

		if (Mode == EMessageTagAdd::Child || Mode == EMessageTagAdd::Root)
		{
			AddNewTagDialog->AddSubtagFromParent(TagName, TagSource);
		}
		else if (Mode == EMessageTagAdd::Duplicate)
		{
			AddNewTagDialog->AddDuplicate(TagName, TagSource);
		}

		NewTagWindow->SetContent(SNew(SBox)
			.MinDesiredWidth(320.0f)
			[
				AddNewTagDialog
			]);
    }
    else if (bRestrictedTags)
    {
        TSharedRef<SAddNewRestrictedMessageTagWidget> AddNewTagDialog = SNew(SAddNewRestrictedMessageTagWidget)
			.OnRestrictedMessageTagAdded_Lambda([WeakSelf, NewTagWindow](const FString& TagName, const FString& TagComment, const FName& TagSource)
			{
				if (const TSharedPtr<SMessageTagPicker> Self = WeakSelf.Pin())
				{
					Self->OnMessageTagAdded(TagName, TagComment, TagSource);
				}
				if (NewTagWindow.IsValid())
				{
					NewTagWindow->RequestDestroyWindow();
				}
			});

    	if (Mode == EMessageTagAdd::Child || Mode == EMessageTagAdd::Root)
    	{
	        AddNewTagDialog->AddSubtagFromParent(TagName, TagSource, bTagAllowsNonRestrictedChildren);
    	}
    	else if (Mode == EMessageTagAdd::Duplicate)
    	{
    		AddNewTagDialog->AddDuplicate(TagName, TagSource, bTagAllowsNonRestrictedChildren);
    	}
        
    	NewTagWindow->SetContent(SNew(SBox)
			.MinDesiredWidth(320.0f)
			[
				AddNewTagDialog
			]);
    }
	
	TSharedPtr<SWindow> CurrentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
	FSlateApplication::Get().AddModalWindow(NewTagWindow.ToSharedRef(), CurrentWindow);
}

void SMessageTagPicker::ShowInlineAddTagWidget(const EMessageTagAdd Mode, TSharedPtr<FMessageTagNode> InTagNode)
{
	UMessageTagsManager& Manager = UMessageTagsManager::Get();
	
	FString TagName;
	FName TagFName;
	FString TagComment;
	FName TagSource;
	bool bTagIsExplicit;
	bool bTagIsRestricted;
	bool bTagAllowsNonRestrictedChildren;

	if (InTagNode.IsValid())
	{
		TagName = InTagNode->GetCompleteTagString();
		TagFName = InTagNode->GetCompleteTagName();
	}

	Manager.GetTagEditorData(TagFName, TagComment, TagSource, bTagIsExplicit, bTagIsRestricted, bTagAllowsNonRestrictedChildren);

	if (AddNewTagWidget.IsValid())
	{
		if (Mode == EMessageTagAdd::Child || Mode == EMessageTagAdd::Root)
		{
			AddNewTagWidget->AddSubtagFromParent(TagName, TagSource);
		}
		else if (Mode == EMessageTagAdd::Duplicate)
		{
			AddNewTagWidget->AddDuplicate(TagName, TagSource);
		}
		bNewTagWidgetVisible = true;
	}
}

FReply SMessageTagPicker::OnAddRootTagClicked()
{
	OpenAddTagDialog(EMessageTagAdd::Root);
	
	return FReply::Handled();
}

TSharedRef<SWidget> SMessageTagPicker::MakeTagActionsMenu(TSharedPtr<FMessageTagNode> InTagNode, TSharedPtr<SComboButton> ActionsCombo, bool bInShouldCloseWindowAfterMenuSelection)
{
	if (!InTagNode.IsValid())
	{
		return SNullWidget::NullWidget;
	}
	
	bool bShowManagement = ((MessageTagPickerMode == EMessageTagPickerMode::ManagementMode || MessageTagPickerMode == EMessageTagPickerMode::HybridMode) && !bReadOnly);
	UMessageTagsManager& Manager = UMessageTagsManager::Get();

	if (!Manager.ShouldImportTagsFromINI())
	{
		bShowManagement = false;
	}

	// You can't modify restricted tags in the normal tag menus
	if (!bRestrictedTags && InTagNode->IsRestrictedMessageTag())
	{
		bShowManagement = false;
	}

	// Do not close menu after selection. The close deletes this widget before action is executed leading to no action being performed.
	// Occurs when SMessageTagPicker is being used as a menu item itself (Details panel of blueprint editor for example).
	FMenuBuilder MenuBuilder(bInShouldCloseWindowAfterMenuSelection, nullptr);

	// Add child tag
	MenuBuilder.AddMenuEntry(LOCTEXT("MessageTagPicker_AddSubTag", "Add Sub Tag"),
		LOCTEXT("MessageTagPicker_AddSubTagTagTooltip", "Add sub tag under selected tag."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus"),
		FUIAction(FExecuteAction::CreateSP(this, &SMessageTagPicker::OnAddSubTag, InTagNode, ActionsCombo), FCanExecuteAction::CreateSP(this, &SMessageTagPicker::CanAddNewSubTag, InTagNode)));

	// Duplicate
	MenuBuilder.AddMenuEntry(LOCTEXT("MessageTagPicker_DuplicateTag", "Duplicate Tag"),
		LOCTEXT("MessageTagPicker_DuplicateTagTooltip", "Duplicate selected tag to create a new tag."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Duplicate"),
		FUIAction(FExecuteAction::CreateSP(this, &SMessageTagPicker::OnDuplicateTag, InTagNode, ActionsCombo), FCanExecuteAction::CreateSP(this, &SMessageTagPicker::CanAddNewSubTag, InTagNode)));

	MenuBuilder.AddSeparator();

	if (bShowManagement)
	{
		// Rename
		MenuBuilder.AddMenuEntry(LOCTEXT("MessageTagPicker_RenameTag", "Rename Tag"),
			LOCTEXT("MessageTagPicker_RenameTagTooltip", "Rename this tag"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Edit"),
			FUIAction(FExecuteAction::CreateSP(this, &SMessageTagPicker::OnRenameTag, InTagNode, ActionsCombo), FCanExecuteAction::CreateSP(this, &SMessageTagPicker::CanModifyTag, InTagNode)));

		// Delete
		MenuBuilder.AddMenuEntry(LOCTEXT("MessageTagPicker_DeleteTag", "Delete Tag"),
			LOCTEXT("MessageTagPicker_DeleteTagTooltip", "Delete this tag"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
			FUIAction(FExecuteAction::CreateSP(this, &SMessageTagPicker::OnDeleteTag, InTagNode, ActionsCombo), FCanExecuteAction::CreateSP(this, &SMessageTagPicker::CanModifyTag, InTagNode)));

		MenuBuilder.AddSeparator();
	}

	// Only include these menu items if we have tag containers to modify
	if (bMultiSelect)
	{
		// Either Selector or Unselect Exact Tag depending on if we have the exact tag or not
		if (IsExactTagInCollection(InTagNode))
		{
			MenuBuilder.AddMenuEntry(LOCTEXT("MessageTagPicker_UnselectTag", "Unselect Exact Tag"),
				LOCTEXT("MessageTagPicker_RemoveTagTooltip", "Unselect this exact tag, Parent and Child Tags will not be effected."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SMessageTagPicker::OnUnselectExactTag, InTagNode, ActionsCombo)));
		}
		else
		{
			MenuBuilder.AddMenuEntry(LOCTEXT("MessageTagPicker_SelectTag", "Select Exact Tag"),
				LOCTEXT("MessageTagPicker_AddTagTooltip", "Select this exact tag, Parent and Child Child Tags will not be effected."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SMessageTagPicker::OnSelectExactTag, InTagNode, ActionsCombo)));
		}

		MenuBuilder.AddSeparator();
	}

	if (!bShowManagement)
	{
		// Open tag in manager
		MenuBuilder.AddMenuEntry(LOCTEXT("MessageTagPicker_OpenTagInManager", "Open Tag in Manager..."),
			LOCTEXT("MessageTagPicker_OpenTagInManagerTooltip", "Opens the Message Tag manage and hilights the selected tag."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"),
			FUIAction(FExecuteAction::CreateRaw(this, &SMessageTagPicker::OnManageTagsClicked, InTagNode, TSharedPtr<SComboButton>())));
	}

	// Search for References
	if (FEditorDelegates::OnOpenReferenceViewer.IsBound())
	{
		MenuBuilder.AddMenuEntry(LOCTEXT("MessageTagPicker_SearchForReferences", "Search For References"),
		FText::Format(LOCTEXT("MessageTagPicker_SearchForReferencesTooltip", "Find references to the tag {0}"), FText::AsCultureInvariant(InTagNode->GetCompleteTagString())),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Search"),
			FUIAction(FExecuteAction::CreateSP(this, &SMessageTagPicker::OnSearchForReferences, InTagNode, ActionsCombo)));
	}

	if (InTagNode->IsExplicitTag())
	{
		MenuBuilder.AddMenuEntry(LOCTEXT("MessageTagWidget_FindInBlueprints", "FindInBlueprints"),
								 LOCTEXT("MessageTagWidget_FindInBlueprintsTooltip", "Find references In Blueprints"),
								 FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Search"),
								 FUIAction(FExecuteAction::CreateSP(this, &SMessageTagPicker::OnSearchMessage, InTagNode)));
	}

	// Copy Name to Clipboard
	MenuBuilder.AddMenuEntry(LOCTEXT("MessageTagPicker_CopyNameToClipboard", "Copy Name to Clipboard"),
	FText::Format(LOCTEXT("MessageTagPicker_CopyNameToClipboardTooltip", "Copy tag {0} to clipboard"), FText::AsCultureInvariant(InTagNode->GetCompleteTagString())),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Copy"),
		FUIAction(FExecuteAction::CreateSP(this, &SMessageTagPicker::OnCopyTagNameToClipboard, InTagNode, ActionsCombo)));

	return MenuBuilder.MakeWidget();
}

bool SMessageTagPicker::CanModifyTag(TSharedPtr<FMessageTagNode> Node) const
{
	if (Node.IsValid())
	{
		// we can only modify tags if they came from an ini file
		if (Node->GetFirstSourceName().ToString().EndsWith(TEXT(".ini")))
		{
			return true;
		}
	}
	return false;
}

void SMessageTagPicker::OnAddSubTag(TSharedPtr<FMessageTagNode> InTagNode, TSharedPtr<SComboButton> OwnerCombo)
{
	if (InTagNode.IsValid())
	{
		ShowInlineAddTagWidget(EMessageTagAdd::Child, InTagNode);
	}

	if (OwnerCombo.IsValid())
	{
		OwnerCombo->SetIsOpen(false);
	}
}

void SMessageTagPicker::OnDuplicateTag(TSharedPtr<FMessageTagNode> InTagNode, TSharedPtr<SComboButton> OwnerCombo)
{
	if (InTagNode.IsValid())
	{
		ShowInlineAddTagWidget(EMessageTagAdd::Duplicate, InTagNode);
	}
	
	if (OwnerCombo.IsValid())
	{
		OwnerCombo->SetIsOpen(false);
	}
}

void SMessageTagPicker::OnRenameTag(TSharedPtr<FMessageTagNode> InTagNode, TSharedPtr<SComboButton> OwnerCombo)
{
	if (InTagNode.IsValid())
	{
		OpenRenameMessageTagDialog(InTagNode);
	}
	
	if (OwnerCombo.IsValid())
	{
		OwnerCombo->SetIsOpen(false);
	}
}

void SMessageTagPicker::OnDeleteTag(TSharedPtr<FMessageTagNode> InTagNode, TSharedPtr<SComboButton> OwnerCombo)
{
	if (InTagNode.IsValid())
	{
		IMessageTagsEditorModule& TagsEditor = IMessageTagsEditorModule::Get();

		bool bTagRemoved = false;
		if (MessageTagPickerMode == EMessageTagPickerMode::HybridMode)
		{
			for (FMessageTagContainer& Container : TagContainers)
			{
				bTagRemoved |= Container.RemoveTag(InTagNode->GetCompleteTag());
			}
		}

		const bool bDeleted = TagsEditor.DeleteTagFromINI(InTagNode);

		if (bDeleted || bTagRemoved)
		{
			OnTagChanged.ExecuteIfBound(TagContainers);
		}
	}
	
	if (OwnerCombo.IsValid())
	{
		OwnerCombo->SetIsOpen(false);
	}
}

void SMessageTagPicker::OnSelectExactTag(TSharedPtr<FMessageTagNode> InTagNode, TSharedPtr<SComboButton> OwnerCombo)
{
	FScopedTransaction Transaction(LOCTEXT("MessageTagPicker_SelectTags", "Select Message Tags"));

	if (InTagNode.IsValid())
	{
		for (FMessageTagContainer& Container : TagContainers)
		{
			Container.AddTag(InTagNode->GetCompleteTag());
		}
	}

	OnContainersChanged();
	
	if (OwnerCombo.IsValid())
	{
		OwnerCombo->SetIsOpen(false);
	}
}

void SMessageTagPicker::OnUnselectExactTag(TSharedPtr<FMessageTagNode> InTagNode, TSharedPtr<SComboButton> OwnerCombo)
{
	FScopedTransaction Transaction(LOCTEXT("MessageTagPicker_SelectTags", "Select Message Tags"));

	if (InTagNode.IsValid())
	{
		for (FMessageTagContainer& Container : TagContainers)
		{
			Container.RemoveTag(InTagNode->GetCompleteTag());
		}
	}

	OnContainersChanged();

	if (OwnerCombo.IsValid())
	{
		OwnerCombo->SetIsOpen(false);
	}
}

void SMessageTagPicker::OnSearchForReferences(TSharedPtr<FMessageTagNode> InTagNode, TSharedPtr<SComboButton> OwnerCombo)
{
	if (InTagNode.IsValid())
	{
		TArray<FAssetIdentifier> AssetIdentifiers;
		AssetIdentifiers.Add(FAssetIdentifier(FMessageTag::StaticStruct(), InTagNode->GetCompleteTagName()));
		FEditorDelegates::OnOpenReferenceViewer.Broadcast(AssetIdentifiers, FReferenceViewerParams());
	}
	
	if (OwnerCombo.IsValid())
	{
		OwnerCombo->SetIsOpen(false);
	}
}

void SMessageTagPicker::OnSearchMessage(TSharedPtr<FMessageTagNode> InTagNode)
{
	if (InTagNode.IsValid() && InTagNode->IsExplicitTag())
	{
		extern void MesageTagsEditor_FindMessageInBlueprints(const FString& MessageKey, class UBlueprint* Blueprint = nullptr);
		MesageTagsEditor_FindMessageInBlueprints(InTagNode->GetCompleteTagString());
	}
}


void SMessageTagPicker::OnCopyTagNameToClipboard(TSharedPtr<FMessageTagNode> InTagNode, TSharedPtr<SComboButton> OwnerCombo)
{
	if (InTagNode.IsValid())
	{
		const FString TagName = InTagNode->GetCompleteTagString();
		FPlatformApplicationMisc::ClipboardCopy(*TagName);
	}
	
	if (OwnerCombo.IsValid())
	{
		OwnerCombo->SetIsOpen(false);
	}
}

void SMessageTagPicker::SetTagTreeItemExpansion(bool bExpand, bool bPersistExpansion)
{
	TArray<TSharedPtr<FMessageTagNode>> TagArray;
	UMessageTagsManager::Get().GetFilteredMessageRootTags(TEXT(""), TagArray);
	for (int32 TagIdx = 0; TagIdx < TagArray.Num(); ++TagIdx)
	{
		SetTagNodeItemExpansion(TagArray[TagIdx], bExpand, bPersistExpansion);
	}
}

void SMessageTagPicker::SetTagNodeItemExpansion(TSharedPtr<FMessageTagNode> Node, bool bExpand, bool bPersistExpansion)
{
	TGuardValue<bool> PersistExpansionChangeGuard(bPersistExpansionChange, bPersistExpansion);
	if (Node.IsValid() && TagTreeWidget.IsValid())
	{
		TagTreeWidget->SetItemExpansion(Node, bExpand);

		const TArray<TSharedPtr<FMessageTagNode>>& ChildTags = Node->GetChildTagNodes();
		for (int32 ChildIdx = 0; ChildIdx < ChildTags.Num(); ++ChildIdx)
		{
			SetTagNodeItemExpansion(ChildTags[ChildIdx], bExpand, bPersistExpansion);
		}
	}
}

void SMessageTagPicker::LoadSettings()
{
	MigrateSettings();

	CachedExpandedItems.Reset();
	TArray<TSharedPtr<FMessageTagNode>> TagArray;
	UMessageTagsManager::Get().GetFilteredMessageRootTags(TEXT(""), TagArray);
	for (int32 TagIdx = 0; TagIdx < TagArray.Num(); ++TagIdx)
	{
		LoadTagNodeItemExpansion(TagArray[TagIdx]);
	}
}

const FString& SMessageTagPicker::GetMessageTagsEditorStateIni()
{
	static FString Filename;

	if (Filename.Len() == 0)
	{
		Filename = FConfigCacheIni::NormalizeConfigIniPath(FString::Printf(TEXT("%s%hs/MessageTagsEditorState.ini"), *FPaths::GeneratedConfigDir(), FPlatformProperties::PlatformName()));
	}

	return Filename;
}

void SMessageTagPicker::MigrateSettings()
{
	if (FConfigSection* EditorPerProjectIniSection = GConfig->GetSectionPrivate(*SettingsIniSection, /*Force=*/false, /*Const=*/true, GEditorPerProjectIni))
	{
		if (EditorPerProjectIniSection->Num() > 0)
		{
			FConfigSection* DestinationSection = GConfig->GetSectionPrivate(*SettingsIniSection, /*Force=*/true, /*Const=*/false, GetMessageTagsEditorStateIni());

			DestinationSection->Reserve(DestinationSection->Num() + EditorPerProjectIniSection->Num());
			for (const auto& It : *EditorPerProjectIniSection)
			{
				DestinationSection->FindOrAdd(It.Key, It.Value);
			}

			GConfig->Flush(false, GetMessageTagsEditorStateIni());
		}

		GConfig->EmptySection(*SettingsIniSection, GEditorPerProjectIni);
		GConfig->Flush(false, GEditorPerProjectIni);
	}
}

void SMessageTagPicker::SetDefaultTagNodeItemExpansion(TSharedPtr<FMessageTagNode> Node)
{
	TGuardValue<bool> PersistExpansionChangeGuard(bPersistExpansionChange, false);
	if (Node.IsValid() && TagTreeWidget.IsValid())
	{
		const bool bIsExpanded = CachedExpandedItems.Contains(Node) || IsTagChecked(Node) == ECheckBoxState::Checked;
		TagTreeWidget->SetItemExpansion(Node, bIsExpanded);
		
		const TArray<TSharedPtr<FMessageTagNode>>& ChildTags = Node->GetChildTagNodes();
		for (int32 ChildIdx = 0; ChildIdx < ChildTags.Num(); ++ChildIdx)
		{
			SetDefaultTagNodeItemExpansion(ChildTags[ChildIdx]);
		}
	}
}

void SMessageTagPicker::LoadTagNodeItemExpansion(TSharedPtr<FMessageTagNode> Node)
{
	TGuardValue<bool> PersistExpansionChangeGuard(bPersistExpansionChange, false);
	if (Node.IsValid() && TagTreeWidget.IsValid())
	{
		bool bIsExpanded = false;

		if (GConfig->GetBool(*SettingsIniSection, *(SettingsName + Node->GetCompleteTagString() + TEXT(".Expanded")), bIsExpanded, GetMessageTagsEditorStateIni()))
		{
			TagTreeWidget->SetItemExpansion(Node, bIsExpanded);
			if (bIsExpanded)
			{
				CachedExpandedItems.Add(Node);
			}
		}
		else if (IsTagChecked(Node) == ECheckBoxState::Checked) // If we have no save data but its ticked then we probably lost our settings so we shall expand it
		{
			TagTreeWidget->SetItemExpansion(Node, true);
		}

		const TArray<TSharedPtr<FMessageTagNode>>& ChildTags = Node->GetChildTagNodes();
		for (int32 ChildIdx = 0; ChildIdx < ChildTags.Num(); ++ChildIdx)
		{
			LoadTagNodeItemExpansion(ChildTags[ChildIdx]);
		}
	}
}

void SMessageTagPicker::OnExpansionChanged(TSharedPtr<FMessageTagNode> InItem, bool bIsExpanded)
{
	if (bPersistExpansionChange)
	{
		// Save the new expansion setting to ini file
		GConfig->SetBool(*SettingsIniSection, *(SettingsName + InItem->GetCompleteTagString() + TEXT(".Expanded")), bIsExpanded, GetMessageTagsEditorStateIni());

		if (bIsExpanded)
		{
			CachedExpandedItems.Add(InItem);
		}
		else
		{
			CachedExpandedItems.Remove(InItem);
		}
	}
}

void SMessageTagPicker::OnContainersChanged()
{
	if (PropertyHandle.IsValid() && bMultiSelect)
	{
		// Case for a tag container
		TArray<FString> PerObjectValues;
		for (const FMessageTagContainer& Container : TagContainers)
		{
			PerObjectValues.Push(Container.ToString());
		}
		PropertyHandle->SetPerObjectValues(PerObjectValues);
	}
	else if (PropertyHandle.IsValid() && !bMultiSelect)
	{
		// Case for a single Tag		
		FString FormattedString = TEXT("(TagName=\"");
		FormattedString += TagContainers[0].First().GetTagName().ToString();
		FormattedString += TEXT("\")");
		PropertyHandle->SetValueFromFormattedString(FormattedString);
	}

	OnTagChanged.ExecuteIfBound(TagContainers);
}

void SMessageTagPicker::OnMessageTagAdded(const FString& TagName, const FString& TagComment, const FName& TagSource)
{
	UMessageTagsManager& Manager = UMessageTagsManager::Get();

	// Make sure the new tag is visible.
	TSharedPtr<FMessageTagNode> TagNode = Manager.FindTagNode(FName(*TagName));
	TSharedPtr<FMessageTagNode> ParentTagNode = TagNode;
	while (ParentTagNode.IsValid())
	{
		const FString Key = SettingsName + ParentTagNode->GetCompleteTagString() + TEXT(".Expanded");
		GConfig->SetBool(*SettingsIniSection, *Key, true, GetMessageTagsEditorStateIni());
		CachedExpandedItems.Add(ParentTagNode);
		
		ParentTagNode = ParentTagNode->GetParentTagNode();
	}

	RefreshTags();
	TagTreeWidget->RequestTreeRefresh();

	if (TagNode.IsValid())
	{
		TGuardValue<bool> PersistExpansionChangeGuard(bInSelectionChanged, true);

		TagTreeWidget->ClearSelection();
		TagTreeWidget->SetItemSelection(TagNode, true);
		
		if (TagNode->CompleteTagWithParents.Num() > 0)
		{
			RequestScrollToView(TagNode->CompleteTagWithParents.GetByIndex(0));
		}
	}
}

void SMessageTagPicker::RefreshTags()
{
	UMessageTagsManager& Manager = UMessageTagsManager::Get();
	Manager.GetFilteredMessageRootTags(RootFilterString, TagItems);

	if (bRestrictedTags)
	{
		// We only want to show the restricted Message tags
		for (int32 Idx = TagItems.Num() - 1; Idx >= 0; --Idx)
		{
			if (!TagItems[Idx]->IsRestrictedMessageTag())
			{
				TagItems.RemoveAtSwap(Idx);
			}
		}
	}

	if (Manager.OnFilterMessageTag.IsBound())
	{
		for (int32 Idx = TagItems.Num() - 1; Idx >= 0; --Idx)
		{
			bool DelegateShouldHide = false;
			FMessageTagSource* Source = Manager.FindTagSource(TagItems[Idx]->GetFirstSourceName());
			Manager.OnFilterMessageTag.Broadcast(UMessageTagsManager::FFilterMessageTagContext(RootFilterString, TagItems[Idx], Source, PropertyHandle), DelegateShouldHide);
			if (DelegateShouldHide)
			{
				TagItems.RemoveAtSwap(Idx);
			}
		}
	}

	// Restore expansion state.
	CachedExpandedItems.Reset();
	for (int32 TagIdx = 0; TagIdx < TagItems.Num(); ++TagIdx)
	{
		LoadTagNodeItemExpansion(TagItems[TagIdx]);
	}

	FilterTagTree();

	OnRefreshTagContainers.ExecuteIfBound(*this);
}

EVisibility SMessageTagPicker::DetermineExpandableUIVisibility() const
{
	const UMessageTagsManager& Manager = UMessageTagsManager::Get();

	if (!Manager.ShouldImportTagsFromINI() || MessageTagPickerMode == EMessageTagPickerMode::SelectionMode)
	{
		// If we can't support adding tags from INI files, or both options are forcibly disabled, we should never see this widget
		return EVisibility::Collapsed;
	}

	return EVisibility::Visible;
}


bool SMessageTagPicker::CanAddNewTag() const
{
	const UMessageTagsManager& Manager = UMessageTagsManager::Get();
	return !bReadOnly && Manager.ShouldImportTagsFromINI();
}

bool SMessageTagPicker::CanAddNewSubTag(TSharedPtr<FMessageTagNode> Node) const
{
	if (!Node.IsValid())
	{
		return false;
	}
	
	if (!CanAddNewTag())
	{
		return false;
	}

	// We do not allow you to add child tags under a conflict
	if (Node->bNodeHasConflict || Node->bAncestorHasConflict)
	{
		return false;
	}

	// show if we're dealing with restricted tags exclusively or restricted tags that allow non-restricted children
	if (Node->GetAllowNonRestrictedChildren() || bRestrictedTags)
	{
		return true;
	}

	return false;
}

bool SMessageTagPicker::CanSelectTags() const
{
	return !bReadOnly
			&& (MessageTagPickerMode == EMessageTagPickerMode::SelectionMode
				|| MessageTagPickerMode == EMessageTagPickerMode::HybridMode);
}

void SMessageTagPicker::RefreshOnNextTick()
{
	bDelayRefresh = true;
}

void SMessageTagPicker::RequestScrollToView(const FMessageTag RequestedTag)
{
	RequestedScrollToTag = RequestedTag;
}

void SMessageTagPicker::OnMessageTagRenamed(FString OldTagName, FString NewTagName)
{
	// @todo: replace changed tag?
	OnTagChanged.ExecuteIfBound(TagContainers);
}

void SMessageTagPicker::OpenRenameMessageTagDialog(TSharedPtr<FMessageTagNode> MessageTagNode) const
{
	TSharedRef<SWindow> RenameTagWindow =
		SNew(SWindow)
		.Title(LOCTEXT("RenameTagWindowTitle", "Rename Message Tag"))
		.SizingRule(ESizingRule::Autosized)
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	TSharedRef<SRenameMessageTagDialog> RenameTagDialog =
		SNew(SRenameMessageTagDialog)
		.MessageTagNode(MessageTagNode)
		.OnMessageTagRenamed(const_cast<SMessageTagPicker*>(this), &SMessageTagPicker::OnMessageTagRenamed);

	RenameTagWindow->SetContent(SNew(SBox)
		.MinDesiredWidth(320.0f)
		[
			RenameTagDialog
		]);

	TSharedPtr<SWindow> CurrentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared() );

	FSlateApplication::Get().AddModalWindow(RenameTagWindow, CurrentWindow);
}

TSharedPtr<SWidget> SMessageTagPicker::GetWidgetToFocusOnOpen()
{
	return SearchTagBox;
}

void SMessageTagPicker::SetTagContainers(TConstArrayView<FMessageTagContainer> InTagContainers)
{
	TagContainers = InTagContainers;
}

void SMessageTagPicker::PostUndo(bool bSuccess)
{
	OnRefreshTagContainers.ExecuteIfBound(*this);
}

void SMessageTagPicker::PostRedo(bool bSuccess)
{
	OnRefreshTagContainers.ExecuteIfBound(*this);
}

void SMessageTagPicker::VerifyAssetTagValidity()
{
	UMessageTagsManager& TagsManager = UMessageTagsManager::Get();

	// Find and remove any tags on the asset that are no longer in the library
	bool bChanged = false;
	for (FMessageTagContainer& Container : TagContainers)
	{
		// Use a set instead of a container so we can find and remove None tags
		TSet<FMessageTag> InvalidTags;

		for (auto It = Container.CreateConstIterator(); It; ++It)
		{
			const FMessageTag TagToCheck = *It;

			if (!UMessageTagsManager::Get().RequestMessageTag(TagToCheck.GetTagName(), false).IsValid())
			{
				InvalidTags.Add(*It);
			}
		}

		if (InvalidTags.Num() > 0)
		{
			FString InvalidTagNames;

			for (auto InvalidIter = InvalidTags.CreateConstIterator(); InvalidIter; ++InvalidIter)
			{
				Container.RemoveTag(*InvalidIter);
				InvalidTagNames += InvalidIter->ToString() + TEXT("\n");
			}
			
			bChanged = true;

			FFormatNamedArguments Arguments;
			Arguments.Add(TEXT("Objects"), FText::FromString(InvalidTagNames));
			FText DialogText = FText::Format(LOCTEXT("MessageTagPicker_InvalidTags", "Invalid Tags that have been removed: \n\n{Objects}"), Arguments);
			FText DialogTitle = LOCTEXT("MessageTagPicker_Warning", "Warning");
#if UE_VERSION_NEWER_THAN(5, 3, 0)
			FMessageDialog::Open(EAppMsgType::Ok, DialogText, DialogTitle);
#else
			FMessageDialog::Open(EAppMsgType::Ok, DialogText, &DialogTitle);
#endif
		}
	}

	if (bChanged)
	{
		OnContainersChanged();
	}
}

void SMessageTagPicker::GetFilteredMessageRootTags(const FString& InFilterString, TArray<TSharedPtr<FMessageTagNode>>& OutNodes) const
{
	OutNodes.Empty();
	const UMessageTagsManager& Manager = UMessageTagsManager::Get();

	if (TagFilter.IsBound())
	{
		TArray<TSharedPtr<FMessageTagNode>> UnfilteredItems;
		Manager.GetFilteredMessageRootTags(InFilterString, UnfilteredItems);
		for (const TSharedPtr<FMessageTagNode>& Node : UnfilteredItems)
		{
			if (TagFilter.Execute(Node) == ETagFilterResult::IncludeTag)
			{
				OutNodes.Add(Node);
			}
		}
	}
	else
	{
		Manager.GetFilteredMessageRootTags(InFilterString, OutNodes);
	}
}

namespace UE::MessageTags::Editor
{

static TWeakPtr<SMessageTagPicker> GlobalTagWidget;
static TWeakPtr<SWindow> GlobalTagWidgetWindow;

void CloseMessageTagWindow(TWeakPtr<SMessageTagPicker> TagWidget)
{
	if (GlobalTagWidget.IsValid() && GlobalTagWidgetWindow.IsValid())
	{
		if (!TagWidget.IsValid() || TagWidget == GlobalTagWidget)
		{
			GlobalTagWidgetWindow.Pin()->RequestDestroyWindow();
		}
	}
	
	GlobalTagWidgetWindow = nullptr;
	GlobalTagWidget = nullptr;
}

TWeakPtr<SMessageTagPicker> OpenMessageTagManager(const FMessageTagManagerWindowArgs& Args)
{
	TSharedPtr<SWindow> MessageTagPickerWindow = GlobalTagWidgetWindow.Pin();
	TSharedPtr<SMessageTagPicker> TagWidget = GlobalTagWidget.Pin();
	
	if (!GlobalTagWidgetWindow.IsValid()
		|| !GlobalTagWidget.IsValid())
	{
		// Close all other MessageTag windows.
		CloseMessageTagWindow(nullptr);

		const FVector2D WindowSize(800, 800);
		
		TagWidget = SNew(SMessageTagPicker)
			.Filter(Args.Filter)
			.ReadOnly(false)
			.MaxHeight(0.0f) // unbounded
			.MultiSelect(false)
			.SettingsName(TEXT("Manager"))
			.MessageTagPickerMode(EMessageTagPickerMode::ManagementMode)
			.RestrictedTags(Args.bRestrictedTags)
		;

		FText Title = Args.Title;
		if (Title.IsEmpty())
		{
			Title = LOCTEXT("MessageTagPicker_ManagerTitle", "Message Tag Manager");
		}
		
		MessageTagPickerWindow = SNew(SWindow)
			.Title(Title)
			.HasCloseButton(true)
			.SupportsMaximize(false)
			.SupportsMinimize(false)
			.AutoCenter(EAutoCenter::PreferredWorkArea)
			.ClientSize(WindowSize)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.FillHeight(1)
					[
						TagWidget.ToSharedRef()
					]
				]
			];

		TWeakPtr<SMessageTagPicker> WeakTagWidget = TagWidget;
		
		// NOTE: FGlobalTabmanager::Get()-> is actually dereferencing a SharedReference, not a SharedPtr, so it cannot be null.
		if (FGlobalTabmanager::Get()->GetRootWindow().IsValid())
		{
			FSlateApplication::Get().AddWindowAsNativeChild(MessageTagPickerWindow.ToSharedRef(), FGlobalTabmanager::Get()->GetRootWindow().ToSharedRef());
		}
		else
		{
			FSlateApplication::Get().AddWindow(MessageTagPickerWindow.ToSharedRef());
		}

		GlobalTagWidget = TagWidget;
		GlobalTagWidgetWindow = MessageTagPickerWindow;
	}

	check (TagWidget.IsValid());

	// Set focus to the search box on creation
	FSlateApplication::Get().SetKeyboardFocus(TagWidget->GetWidgetToFocusOnOpen());
	FSlateApplication::Get().SetUserFocus(0, TagWidget->GetWidgetToFocusOnOpen());

	if (Args.HighlightedTag.IsValid())
	{
		TagWidget->RequestScrollToView(Args.HighlightedTag);
	}

	return TagWidget;
}

}

#undef LOCTEXT_NAMESPACE
