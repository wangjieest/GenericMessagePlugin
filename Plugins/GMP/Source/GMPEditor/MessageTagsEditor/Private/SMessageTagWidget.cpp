// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMessageTagWidget.h"
#include "Misc/ConfigCacheIni.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Images/SImage.h"
#include "EditorStyleSet.h"
#include "Widgets/SWindow.h"
#include "Misc/MessageDialog.h"
#include "MessageTagsModule.h"
#include "ScopedTransaction.h"
#include "Textures/SlateIcon.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SSearchBox.h"
#include "MessageTagsEditorModule.h"
#include "Widgets/Layout/SScaleBox.h"

#include "AssetToolsModule.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "MessageTagsSettings.h"
#include "Layout/WidgetPath.h"
#include "Framework/Application/SlateApplication.h"
#include "SAddNewMessageTagWidget.h"
#include "SAddNewMessageTagSourceWidget.h"
#include "SAddNewRestrictedMessageTagWidget.h"
#include "SRenameMessageTagDialog.h"
#include "Editor.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "AssetManagerEditorModule.h"
#include "Interfaces/IMainFrameModule.h"
#if UE_5_01_OR_LATER
#include "AssetRegistry/AssetRegistryModule.h"
#else
#include "AssetRegistryModule.h"
#endif
#define LOCTEXT_NAMESPACE "MessageTagWidget"

const FString SMessageTagWidget::SettingsIniSection = TEXT("MessageTagWidget");

void SMessageTagWidget::Construct(const FArguments& InArgs, const TArray<FEditableMessageTagContainerDatum>& EditableTagContainers)
{
	// If we're in management mode, we don't need to have editable tag containers.
	ensure(EditableTagContainers.Num() > 0 || InArgs._MessageTagUIMode == EMessageTagUIMode::ManagementMode);
	TagContainers = EditableTagContainers;

	OnTagChanged = InArgs._OnTagChanged;
	bReadOnly = InArgs._ReadOnly;
	TagContainerName = InArgs._TagContainerName;
	bMultiSelect = InArgs._MultiSelect;
	PropertyHandle = InArgs._PropertyHandle;
	RootFilterString = InArgs._Filter;
	MessageTagUIMode = InArgs._MessageTagUIMode;

	bAddTagSectionExpanded = InArgs._NewTagControlsInitiallyExpanded;
	bDelayRefresh = false;
	MaxHeight = InArgs._MaxHeight;

	bRestrictedTags = InArgs._RestrictedTags;
	bShowClearAll = InArgs._bShowClearAll;

	UMessageTagsManager::OnEditorRefreshMessageTagTree.AddSP(this, &SMessageTagWidget::RefreshOnNextTick);
	UMessageTagsManager& Manager = UMessageTagsManager::Get();

	Manager.GetFilteredMessageRootTags(RootFilterString, TagItems);

	if (bRestrictedTags)
	{
		// We only want to show the restricted message tags
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
			FMessageTagSource* Source = Manager.FindTagSource(TagItems[Idx]->SourceName);
			Manager.OnFilterMessageTag.Broadcast(UMessageTagsManager::FFilterMessageTagContext(RootFilterString, TagItems[Idx], Source, PropertyHandle), DelegateShouldHide);
			if (DelegateShouldHide)
			{
				TagItems.RemoveAtSwap(Idx);
			}
		}
	}

	// Tag the assets as transactional so they can support undo/redo
	TArray<UObject*> ObjectsToMarkTransactional;
	if (PropertyHandle.IsValid())
	{
		// If we have a property handle use that to find the objects that need to be transactional
		PropertyHandle->GetOuterObjects(ObjectsToMarkTransactional);
	}
	else
	{
		// Otherwise use the owner list
		for (int32 AssetIdx = 0; AssetIdx < TagContainers.Num(); ++AssetIdx)
		{
			ObjectsToMarkTransactional.Add(TagContainers[AssetIdx].TagContainerOwner.Get());
		}
	}

	// Now actually mark the assembled objects
	for (UObject* ObjectToMark : ObjectsToMarkTransactional)
	{
		if (ObjectToMark)
		{
			ObjectToMark->SetFlags(RF_Transactional);
		}
	}

	const FText NewTagText = bRestrictedTags ? LOCTEXT("AddNewRestrictedTag", "Add New Restricted Message Tag") : LOCTEXT("AddNewTag", "Add New Message Tag");

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FGMPStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)

			// Expandable UI controls
			+SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Top)
			[
				SNew(SHorizontalBox)

				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew( SCheckBox )
					.IsChecked(this, &SMessageTagWidget::GetAddTagSectionExpansionState)
					.OnCheckStateChanged(this, &SMessageTagWidget::OnAddTagSectionExpansionStateChanged)
					.CheckedImage(FGMPStyle::GetBrush("TreeArrow_Expanded"))
					.CheckedHoveredImage(FGMPStyle::GetBrush("TreeArrow_Expanded_Hovered"))
					.CheckedPressedImage(FGMPStyle::GetBrush("TreeArrow_Expanded"))
					.UncheckedImage(FGMPStyle::GetBrush("TreeArrow_Collapsed"))
					.UncheckedHoveredImage(FGMPStyle::GetBrush("TreeArrow_Collapsed_Hovered"))
					.UncheckedPressedImage(FGMPStyle::GetBrush("TreeArrow_Collapsed"))
					.Visibility(this, &SMessageTagWidget::DetermineExpandableUIVisibility)
					[
						SNew( STextBlock )
						.Text( NewTagText )
					]
				]
			]


			// Expandable UI for adding restricted tags
			+SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Top)
			.Padding(16.0f, 0.0f)
			[
				SAssignNew(AddNewRestrictedTagWidget, SAddNewRestrictedMessageTagWidget)
				.Visibility(this, &SMessageTagWidget::DetermineAddNewRestrictedTagWidgetVisibility)
				.OnRestrictedMessageTagAdded(this, &SMessageTagWidget::OnMessageTagAdded)
				.NewRestrictedTagName(InArgs._NewTagName)
			]

			// Expandable UI for adding non-restricted tags
			+ SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Top)
			.Padding(16.0f, 0.0f)
			[
				SAssignNew(AddNewTagWidget, SAddNewMessageTagWidget)
				.Visibility(this, &SMessageTagWidget::DetermineAddNewTagWidgetVisibility)
				.OnMessageTagAdded(this, &SMessageTagWidget::OnMessageTagAdded)
				.NewTagName(InArgs._NewTagName)
			]

			// Expandable UI controls
			+ SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Top)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SCheckBox)
					.IsChecked(this, &SMessageTagWidget::GetAddSourceSectionExpansionState)
					.OnCheckStateChanged(this, &SMessageTagWidget::OnAddSourceSectionExpansionStateChanged)
					.CheckedImage(FGMPStyle::GetBrush("TreeArrow_Expanded"))
					.CheckedHoveredImage(FGMPStyle::GetBrush("TreeArrow_Expanded_Hovered"))
					.CheckedPressedImage(FGMPStyle::GetBrush("TreeArrow_Expanded"))
					.UncheckedImage(FGMPStyle::GetBrush("TreeArrow_Collapsed"))
					.UncheckedHoveredImage(FGMPStyle::GetBrush("TreeArrow_Collapsed_Hovered"))
					.UncheckedPressedImage(FGMPStyle::GetBrush("TreeArrow_Collapsed"))
					.Visibility(this, &SMessageTagWidget::DetermineAddNewSourceExpandableUIVisibility)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("AddNewSource", "Add New Tag Source"))
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Top)
			.Padding(16.0f, 0.0f)
			[
				SAssignNew(AddNewTagSourceWidget, SAddNewMessageTagSourceWidget)
				.Visibility(this, &SMessageTagWidget::DetermineAddNewSourceWidgetVisibility)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Top)
			[
				SNew(SBorder)
				.BorderImage(FGMPStyle::GetBrush("DetailsView.CategoryMiddle"))
				.Padding(FMargin(0.0f, 3.0f, 0.0f, 0.0f))
				.Visibility(this, &SMessageTagWidget::DetermineAddNewTagWidgetVisibility)
				[
					SNew(SImage)
					.Image(FGMPStyle::GetBrush("DetailsView.AdvancedDropdownBorder.Open"))
				]
			]

			// Message Tag Tree controls
			+SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Top)
			[
				SNew(SHorizontalBox)

				// Expand All nodes
				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.OnClicked(this, &SMessageTagWidget::OnExpandAllClicked)
					.Text(LOCTEXT("MessageTagWidget_ExpandAll", "Expand All"))
				]
			
				// Collapse All nodes
				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.OnClicked(this, &SMessageTagWidget::OnCollapseAllClicked)
					.Text(LOCTEXT("MessageTagWidget_CollapseAll", "Collapse All"))
				]

				// Clear selections
				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.IsEnabled(this, &SMessageTagWidget::CanSelectTags)
					.OnClicked(this, &SMessageTagWidget::OnClearAllClicked)
					.Text(LOCTEXT("MessageTagWidget_ClearAll", "Clear All"))
					.Visibility(this, &SMessageTagWidget::DetermineClearSelectionVisibility)
				]

				// Search
				+SHorizontalBox::Slot()
				.VAlign( VAlign_Center )
				.FillWidth(1.f)
				.Padding(5,1,5,1)
				[
					SAssignNew(SearchTagBox, SSearchBox)
					.HintText(LOCTEXT("MessageTagWidget_SearchBoxHint", "Search Message Tags"))
					.OnTextChanged(this, &SMessageTagWidget::OnFilterTextChanged)
				]
			]

			// Message Tags tree
			+SVerticalBox::Slot()
			.MaxHeight(MaxHeight)
			[
				SAssignNew(TagTreeContainerWidget, SBorder)
				.Padding(FMargin(4.f))
				[
					SAssignNew(TagTreeWidget, STreeView<TSharedPtr<FMessageTagNode>>)
					.TreeItemsSource(&TagItems)
					.OnGenerateRow(this, &SMessageTagWidget::OnGenerateRow)
					.OnGetChildren(this, &SMessageTagWidget::OnGetChildren)
					.OnExpansionChanged(this, &SMessageTagWidget::OnExpansionChanged)
					.SelectionMode(ESelectionMode::Multi)
				]
			]
		]
	];

	// Force the entire tree collapsed to start
	SetTagTreeItemExpansion(false);
	if (!InArgs._ScrollTo.IsNone())
	{
		FString MsgTagNameStr = InArgs._ScrollTo.ToString();
		TArray<FString> Cells;
		MsgTagNameStr.ParseIntoArray(Cells, TEXT("."));
		auto Items = TagItems;
		for (auto& Cell : Cells)
		{
			FName MatchName = *Cell;
			if (auto Find = Items.FindByPredicate([&](TSharedPtr<FMessageTagNode>& Item) { return Item->GetSimpleTagName() == MatchName; }))
			{
				SetTagNodeItemExpansion(*Find, true);
				TagTreeWidget->RequestScrollIntoView(*Find);
				Items = (*Find)->GetChildTagNodes();
			}
			else
			{
				break;
			}
		}
	}
	//FSlateApplication::Get().SetAllUserFocus(SearchTagBox.ToSharedRef());
	DeferredSetFcous();
	LoadSettings();

	// Strip any invalid tags from the assets being edited
	VerifyAssetTagValidity();
}

void SMessageTagWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (bDelayRefresh)
	{
		RefreshTags();
		bDelayRefresh = false;
	}
	if (DeferredActions.Num() > 0)
	{
		for (int32 ActionIndex = 0; ActionIndex < DeferredActions.Num(); ++ActionIndex)
		{
			DeferredActions[ActionIndex].ExecuteIfBound();
		}
		DeferredActions.Empty();
	}
}

void SMessageTagWidget::EnqueueDeferredAction(FSimpleDelegate cb)
{
	DeferredActions.Emplace(MoveTemp(cb));
}

void SMessageTagWidget::DeferredSetFcous()
{
	EnqueueDeferredAction(CreateWeakLambda(this, [this] { FSlateApplication::Get().SetAllUserFocus(SearchTagBox.ToSharedRef()); }));
}

FVector2D SMessageTagWidget::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	FVector2D WidgetSize = SCompoundWidget::ComputeDesiredSize(LayoutScaleMultiplier);

	FVector2D TagTreeContainerSize = TagTreeContainerWidget->GetDesiredSize();

	if (TagTreeContainerSize.Y < MaxHeight)
	{
		WidgetSize.Y += MaxHeight - TagTreeContainerSize.Y;
	}

	return WidgetSize;
}

void SMessageTagWidget::OnFilterTextChanged(const FText& InFilterText)
{
	FilterString = InFilterText.ToString();	

	FilterTagTree();
}

void SMessageTagWidget::FilterTagTree()
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

bool SMessageTagWidget::FilterChildrenCheck(TSharedPtr<FMessageTagNode> InItem)
{
	if( !InItem.IsValid() )
	{
		return false;
	}

	if (bRestrictedTags && !InItem->IsRestrictedMessageTag())
	{
		return false;
	}

	auto FilterChildrenCheck_r = ([=]()
	{
		TArray<TSharedPtr<FMessageTagNode>> Children = InItem->GetChildTagNodes();
		for( int32 iChild = 0; iChild < Children.Num(); ++iChild )
		{
			if( FilterChildrenCheck( Children[iChild] ) )
			{
				return true;
			}
		}
		return false;
	});

	UMessageTagsManager& Manager = UMessageTagsManager::Get();
	bool DelegateShouldHide = false;
	Manager.OnFilterMessageTagChildren.Broadcast(RootFilterString, InItem, DelegateShouldHide);
#if 0
	if (!DelegateShouldHide && Manager.OnFilterMessageTag.IsBound())
	{
		FMessageTagSource* Source = Manager.FindTagSource(InItem->SourceName);
		Manager.OnFilterMessageTag.Broadcast(UMessageTagsManager::FFilterMessageTagContext(RootFilterString, InItem, Source, PropertyHandle), DelegateShouldHide);
	}
#endif
	if (DelegateShouldHide)
	{
		// The delegate wants to hide, see if any children need to show
		return FilterChildrenCheck_r();
	}

	if( InItem->GetCompleteTagString().Contains( FilterString ) || FilterString.IsEmpty() )
	{
		return true;
	}

	return FilterChildrenCheck_r();
}

TSharedRef<ITableRow> SMessageTagWidget::OnGenerateRow(TSharedPtr<FMessageTagNode> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	FText TooltipText;
	if (InItem.IsValid())
	{
		UMessageTagsManager& Manager = UMessageTagsManager::Get();

		FName TagName = InItem.Get()->GetCompleteTagName();
		TSharedPtr<FMessageTagNode> Node = Manager.FindTagNode(TagName);

		TStringBuilder<1024> TooltipString;
		TagName.ToString(TooltipString);

		if (Node.IsValid())
		{
			// Add Tag source if we're in management mode
			if (EnumHasAllFlags(MessageTagUIMode, EMessageTagUIMode::ManagementMode))
			{
				FName TagSource;

				if (Node->bIsExplicitTag)
				{
					TagSource = Node->SourceName;
				}
				else
				{
					TagSource = FName(TEXT("Implicit"));
				}

				TooltipString.Appendf(TEXT(" (%s)"), *TagSource.ToString());
			}

			// parameters
			if (Node->Parameters.Num() > 0)
			{
				TooltipString.Append(TEXT("\n\n("));
				for (auto i = 0; i < Node->Parameters.Num() - 1; ++i)
				{
					auto& Parameter = Node->Parameters[i];
					TooltipString.Appendf(TEXT("%s %s, "), *Parameter.Type.ToString(), *Parameter.Name.ToString());
				}
				TooltipString.Appendf(TEXT("%s %s)"), *Node->Parameters.Last().Type.ToString(), *Node->Parameters.Last().Name.ToString());
			}
			
			// respone Types
			if (Node->ResponseTypes.Num() > 0)
			{
				TooltipString.Append(TEXT("\n\n("));
				auto& ValueRef = Node->ResponseTypes;
				for (auto i = 0; i < ValueRef.Num() - 1; ++i)
				{
					auto& ResponeType = ValueRef[i];
					TooltipString.Appendf(TEXT("%s %s, "), *ResponeType.Type.ToString(), *ResponeType.Name.ToString());
				}
				TooltipString.Appendf(TEXT("%s %s)"), *ValueRef.Last().Type.ToString(), *ValueRef.Last().Name.ToString());
			}

			// tag comments
			if (!Node->DevComment.IsEmpty())
			{
				TooltipString.Appendf(TEXT("\n\n%s"), *Node->DevComment);
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

		TooltipText = FText::FromString(*TooltipString);
	}

	return SNew(STableRow<TSharedPtr<FMessageTagNode>>, OwnerTable)
		.Style(FGMPStyle::Get(), "GameplayTagTreeView")
		[
			SNew( SHorizontalBox )

			// Tag Selection (selection mode only)
			+SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Left)
			[
				SNew(SCheckBox)
				.OnCheckStateChanged(this, &SMessageTagWidget::OnTagCheckStatusChanged, InItem)
				.IsChecked(this, &SMessageTagWidget::IsTagChecked, InItem)
				.ToolTipText(TooltipText)
				.IsEnabled(this, &SMessageTagWidget::CanSelectThisTags, InItem)
				.Visibility(!EnumHasAllFlags(MessageTagUIMode, EMessageTagUIMode::ManagementMode) ? EVisibility::Visible : EVisibility::Collapsed)
				[
					SNew(STextBlock)
					.Text(FText::FromName(InItem->GetSimpleTagName()))
				]
			]

			// Normal Tag Display (management mode only)
			+SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Left)
			[
				SNew( STextBlock )
				.ToolTip( FSlateApplication::Get().MakeToolTip(TooltipText) )
				.Text(FText::FromName( InItem->GetSimpleTagName()) )
				.ColorAndOpacity(this, &SMessageTagWidget::GetTagTextColour, InItem)
				.Visibility(EnumHasAllFlags(MessageTagUIMode, EMessageTagUIMode::ManagementMode) ? EVisibility::Visible : EVisibility::Collapsed)
			]

			// Allows non-restricted children checkbox
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("AllowsChildren", "Does this restricted tag allow non-restricted children"))
				.OnCheckStateChanged(this, &SMessageTagWidget::OnAllowChildrenTagCheckStatusChanged, InItem)
				.IsChecked(this, &SMessageTagWidget::IsAllowChildrenTagChecked, InItem)
				.Visibility(this, &SMessageTagWidget::DetermineAllowChildrenVisible, InItem)
			]

			// Add Subtag
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			[
				SNew( SButton )
				.ToolTipText( LOCTEXT("AddSubtag", "Add Subtag") )
				.Visibility(this, &SMessageTagWidget::DetermineAddNewSubTagWidgetVisibility, InItem)
				.ButtonStyle(FGMPStyle::Get(), "HoverHintOnly")
				.OnClicked(this, &SMessageTagWidget::OnAddSubtagClicked, InItem)
				.DesiredSizeScale(FVector2D(0.75f, 0.75f))
				.ContentPadding(4.0f)
				.ForegroundColor(FSlateColor::UseForeground())
				.IsEnabled( !bReadOnly )
				.IsFocusable( false )
				[
					SNew( SImage )
					.Image(FGMPStyle::GetBrush("PropertyWindow.Button_AddToArray"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]

			// More Actions Menu
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			[
				SNew( SComboButton )
				.ToolTipText( LOCTEXT("MoreActions", "More Actions...") )
				.ButtonStyle(FGMPStyle::Get(), "HoverHintOnly")
				.ContentPadding(0)
				.ForegroundColor(FSlateColor::UseForeground())
				.HasDownArrow(true)
				.OnGetMenuContent(this, &SMessageTagWidget::MakeTagActionsMenu, InItem)
			]
		];
}

void SMessageTagWidget::OnGetChildren(TSharedPtr<FMessageTagNode> InItem, TArray<TSharedPtr<FMessageTagNode>>& OutChildren)
{
	TArray<TSharedPtr<FMessageTagNode>> FilteredChildren;
	TArray<TSharedPtr<FMessageTagNode>> Children = InItem->GetChildTagNodes();

	for( int32 iChild = 0; iChild < Children.Num(); ++iChild )
	{
		if( FilterChildrenCheck( Children[iChild] ) )
		{
			FilteredChildren.Add( Children[iChild] );
		}
	}
	OutChildren += FilteredChildren;
}

void SMessageTagWidget::OnTagCheckStatusChanged(ECheckBoxState NewCheckState, TSharedPtr<FMessageTagNode> NodeChanged)
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

void SMessageTagWidget::OnTagChecked(TSharedPtr<FMessageTagNode> NodeChecked)
{
	FScopedTransaction Transaction(LOCTEXT("MessageTagWidget_AddTags", "Add Message Tags"));

	UMessageTagsManager& TagsManager = UMessageTagsManager::Get();

	for (int32 ContainerIdx = 0; ContainerIdx < TagContainers.Num(); ++ContainerIdx)
	{
		TSharedPtr<FMessageTagNode> CurNode(NodeChecked);
		UObject* OwnerObj = TagContainers[ContainerIdx].TagContainerOwner.Get();
		FMessageTagContainer* Container = TagContainers[ContainerIdx].TagContainer;

		if (Container)
		{
			FMessageTagContainer EditableContainer = *Container;

			bool bRemoveParents = false;

			while (CurNode.IsValid())
			{
				FMessageTag MessageTag = CurNode->GetCompleteTag();

				if (bRemoveParents == false)
				{
					bRemoveParents = true;
					if (bMultiSelect == false)
					{
						EditableContainer.Reset();
					}
					EditableContainer.AddTag(MessageTag);
				}
				else
				{
					EditableContainer.RemoveTag(MessageTag);
				}

				CurNode = CurNode->GetParentTagNode();
			}
			SetContainer(Container, &EditableContainer, OwnerObj);
		}
	}
}

void SMessageTagWidget::OnTagUnchecked(TSharedPtr<FMessageTagNode> NodeUnchecked)
{
	FScopedTransaction Transaction(LOCTEXT("MessageTagWidget_RemoveTags", "Remove Message Tags"));
	if (NodeUnchecked.IsValid())
	{
		UMessageTagsManager& TagsManager = UMessageTagsManager::Get();

		for (int32 ContainerIdx = 0; ContainerIdx < TagContainers.Num(); ++ContainerIdx)
		{
			UObject* OwnerObj = TagContainers[ContainerIdx].TagContainerOwner.Get();
			FMessageTagContainer* Container = TagContainers[ContainerIdx].TagContainer;
			FMessageTag MessageTag = NodeUnchecked->GetCompleteTag();

			if (Container)
			{
				FMessageTagContainer EditableContainer = *Container;
				EditableContainer.RemoveTag(MessageTag);

				TSharedPtr<FMessageTagNode> ParentNode = NodeUnchecked->GetParentTagNode();
				if (ParentNode.IsValid() && (!EnumHasAnyFlags(MessageTagUIMode, EMessageTagUIMode::ExplicitSelMode) || ParentNode->bIsExplicitTag))
				{
					// Check if there are other siblings before adding parent
					bool bOtherSiblings = false;
					for (auto It = ParentNode->GetChildTagNodes().CreateConstIterator(); It; ++It)
					{
						MessageTag = It->Get()->GetCompleteTag();
						if (EditableContainer.HasTagExact(MessageTag))
						{
							bOtherSiblings = true;
							break;
						}
					}
					// Add Parent
					if (!bOtherSiblings)
					{
						MessageTag = ParentNode->GetCompleteTag();
						EditableContainer.AddTag(MessageTag);
					}
				}

				// Uncheck Children
				for (const auto& ChildNode : NodeUnchecked->GetChildTagNodes())
				{
					UncheckChildren(ChildNode, EditableContainer);
				}

				SetContainer(Container, &EditableContainer, OwnerObj);
			}
			
		}
	}
}

void SMessageTagWidget::UncheckChildren(TSharedPtr<FMessageTagNode> NodeUnchecked, FMessageTagContainer& EditableContainer)
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

ECheckBoxState SMessageTagWidget::IsTagChecked(TSharedPtr<FMessageTagNode> Node) const
{
	int32 NumValidAssets = 0;
	int32 NumAssetsTagIsAppliedTo = 0;

	if (Node.IsValid())
	{
		UMessageTagsManager& TagsManager = UMessageTagsManager::Get();

		for (int32 ContainerIdx = 0; ContainerIdx < TagContainers.Num(); ++ContainerIdx)
		{
			if (TagContainers[ContainerIdx].TagContainerOwner.IsStale())
				continue;
			FMessageTagContainer* Container = TagContainers[ContainerIdx].TagContainer;
			if (Container)
			{
				NumValidAssets++;
				FMessageTag MessageTag = Node->GetCompleteTag();
				if (MessageTag.IsValid())
				{
					if (Container->HasTag(MessageTag))
					{
						++NumAssetsTagIsAppliedTo;
					}
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

bool SMessageTagWidget::IsExactTagInCollection(TSharedPtr<FMessageTagNode> Node) const
{
	if (Node.IsValid())
	{
		UMessageTagsManager& TagsManager = UMessageTagsManager::Get();

		for (int32 ContainerIdx = 0; ContainerIdx < TagContainers.Num(); ++ContainerIdx)
		{
			if (TagContainers[ContainerIdx].TagContainerOwner.IsStale())
				continue;
			FMessageTagContainer* Container = TagContainers[ContainerIdx].TagContainer;
			if (Container)
			{
				FMessageTag MessageTag = Node->GetCompleteTag();
				if (MessageTag.IsValid())
				{
					if (Container->HasTagExact(MessageTag))
					{
						return true;
					}
				}
			}
		}
	}

	return false;
}

void SMessageTagWidget::OnAllowChildrenTagCheckStatusChanged(ECheckBoxState NewCheckState, TSharedPtr<FMessageTagNode> NodeChanged)
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

ECheckBoxState SMessageTagWidget::IsAllowChildrenTagChecked(TSharedPtr<FMessageTagNode> Node) const
{
	if (Node->GetAllowNonRestrictedChildren())
	{
		return ECheckBoxState::Checked;
	}

	return ECheckBoxState::Unchecked;
}

EVisibility SMessageTagWidget::DetermineAllowChildrenVisible(TSharedPtr<FMessageTagNode> Node) const
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

FReply SMessageTagWidget::OnClearAllClicked()
{
	FScopedTransaction Transaction(LOCTEXT("MessageTagWidget_RemoveAllTags", "Remove All Message Tags"));

	for (int32 ContainerIdx = 0; ContainerIdx < TagContainers.Num(); ++ContainerIdx)
	{
		UObject* OwnerObj = TagContainers[ContainerIdx].TagContainerOwner.Get();
		FMessageTagContainer* Container = TagContainers[ContainerIdx].TagContainer;

		if (Container)
		{
			FMessageTagContainer EmptyContainer;
			SetContainer(Container, &EmptyContainer, OwnerObj);
		}
	}
	return FReply::Handled();
}

FSlateColor SMessageTagWidget::GetTagTextColour(TSharedPtr<FMessageTagNode> Node) const
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

FReply SMessageTagWidget::OnExpandAllClicked()
{
	SetTagTreeItemExpansion(true);
	return FReply::Handled();
}

FReply SMessageTagWidget::OnCollapseAllClicked()
{
	SetTagTreeItemExpansion(false);
	return FReply::Handled();
}

FReply SMessageTagWidget::OnAddSubtagClicked(TSharedPtr<FMessageTagNode> InTagNode)
{
	if (!bReadOnly && InTagNode.IsValid())
	{
		UMessageTagsManager& Manager = UMessageTagsManager::Get();

		FString TagName = InTagNode->GetCompleteTagString();
		FString TagComment;
		FName TagSource;
		bool bTagIsExplicit;
		bool bTagIsRestricted;
		bool bTagAllowsNonRestrictedChildren;

		Manager.GetTagEditorData(InTagNode->GetCompleteTagName(), TagComment, TagSource, bTagIsExplicit, bTagIsRestricted, bTagAllowsNonRestrictedChildren);

		if (!bRestrictedTags && AddNewTagWidget.IsValid())
		{
			bAddTagSectionExpanded = true; 
			AddNewTagWidget->AddSubtagFromParent(TagName, TagSource);
		}
		else if (bRestrictedTags && AddNewRestrictedTagWidget.IsValid())
		{
			bAddTagSectionExpanded = true;
			AddNewRestrictedTagWidget->AddSubtagFromParent(TagName, TagSource, bTagAllowsNonRestrictedChildren);
		}
	}
	return FReply::Handled();
}

TSharedRef<SWidget> SMessageTagWidget::MakeTagActionsMenu(TSharedPtr<FMessageTagNode> InTagNode)
{
	bool bShowManagement = (EnumHasAllFlags(MessageTagUIMode, EMessageTagUIMode::ManagementMode) && !bReadOnly);
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

	// we can only rename or delete tags if they came from an ini file
	if (!InTagNode->SourceName.ToString().EndsWith(TEXT(".ini")))
	{
		bShowManagement = false;
	}

	// Do not close menu after selection. The close deletes this widget before action is executed leading to no action being performed.
	// Occurs when SMessageTagWidget is being used as a menu item itself (Details panel of blueprint editor for example).
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/ false, nullptr);

	// Rename
	if (bShowManagement || (InTagNode->IsExplicitTag() && (InTagNode->DevComment.IsEmpty() || InTagNode->DevComment == TEXT("CodeGen") || InTagNode->Parameters.Num() > 0 || InTagNode->ResponseTypes.Num() > 0)))
	{
		FExecuteAction RenameAction = FExecuteAction::CreateSP(this, &SMessageTagWidget::OnRenameTag, InTagNode, bShowManagement);

		MenuBuilder.AddMenuEntry(LOCTEXT("MessageTagWidget_RenameTag", "Change"), LOCTEXT("MessageTagWidget_RenameTagTooltip", "Change this tag"), FSlateIcon(), FUIAction(RenameAction));
	}

	// Delete
	if (bShowManagement)
	{
		FExecuteAction DeleteAction = FExecuteAction::CreateSP(this, &SMessageTagWidget::OnDeleteTag, InTagNode);

		MenuBuilder.AddMenuEntry(LOCTEXT("MessageTagWidget_DeleteTag", "Delete"), LOCTEXT("MessageTagWidget_DeleteTagTooltip", "Delete this tag"), FSlateIcon(), FUIAction(DeleteAction));
	}

	// Only include these menu items if we have tag containers to modify
	if (TagContainers.Num() > 0)
	{
		// Either Remove or Add Exact Tag depending on if we have the exact tag or not
		if (IsExactTagInCollection(InTagNode))
		{
			FExecuteAction RemoveAction = FExecuteAction::CreateSP(this, &SMessageTagWidget::OnRemoveTag, InTagNode);
			MenuBuilder.AddMenuEntry(LOCTEXT("MessageTagWidget_RemoveTag", "Remove Exact Tag"),
									 LOCTEXT("MessageTagWidget_RemoveTagTooltip", "Remove this exact tag, Parent and Child Tags will not be effected."),
									 FSlateIcon(),
									 FUIAction(RemoveAction));
		}
		else
		{
			FExecuteAction AddAction = FExecuteAction::CreateSP(this, &SMessageTagWidget::OnAddTag, InTagNode);
			MenuBuilder.AddMenuEntry(LOCTEXT("MessageTagWidget_AddTag", "Add Exact Tag"), LOCTEXT("MessageTagWidget_AddTagTooltip", "Add this exact tag, Parent and Child Child Tags will not be effected."), FSlateIcon(), FUIAction(AddAction));
		}
	}

	// Search for References
#if UE_4_23_OR_LATER
	if (FEditorDelegates::OnOpenReferenceViewer.IsBound())
#else
	if (IAssetManagerEditorModule::IsAvailable())
#endif
	{
		FExecuteAction SearchForReferencesAction = FExecuteAction::CreateSP(this, &SMessageTagWidget::OnSearchForReferences, InTagNode);
		MenuBuilder.AddMenuEntry(LOCTEXT("MessageTagWidget_SearchForReferences", "Search For References"),
								 LOCTEXT("MessageTagWidget_SearchForReferencesTooltip", "Find references for this tag"),
								 FSlateIcon(),
								 FUIAction(SearchForReferencesAction));
	}

	if (InTagNode->IsExplicitTag())
	{
		FExecuteAction FindInBlueprintAction = FExecuteAction::CreateSP(this, &SMessageTagWidget::OnSearchMessage, InTagNode);

		MenuBuilder.AddMenuEntry(LOCTEXT("MessageTagWidget_FindInBlueprints", "FindInBlueprints"), LOCTEXT("MessageTagWidget_FindInBlueprintsTooltip", "Find references In Blueprints"), FSlateIcon(), FUIAction(FindInBlueprintAction));
	}

	return MenuBuilder.MakeWidget();
}

void SMessageTagWidget::OnRenameTag(TSharedPtr<FMessageTagNode> InTagNode, bool bAllowFullEdit)
{
	if (InTagNode.IsValid())
	{
		OpenRenameMessageTagDialog(InTagNode, bAllowFullEdit);
	}
}

void SMessageTagWidget::OnDeleteTag(TSharedPtr<FMessageTagNode> InTagNode)
{
	if (InTagNode.IsValid())
	{
		IMessageTagsEditorModule& TagsEditor = IMessageTagsEditorModule::Get();

		const bool bDeleted = TagsEditor.DeleteTagFromINI(InTagNode);

		if (bDeleted)
		{
			OnTagChanged.ExecuteIfBound();
		}
	}
}

void SMessageTagWidget::OnAddTag(TSharedPtr<FMessageTagNode> InTagNode)
{
	if (InTagNode.IsValid())
	{
		for (int32 ContainerIdx = 0; ContainerIdx < TagContainers.Num(); ++ContainerIdx)
		{
			FMessageTagContainer* Container = TagContainers[ContainerIdx].TagContainer;
			Container->AddTag(InTagNode->GetCompleteTag());
		}

		OnTagChanged.ExecuteIfBound();
	}
}

void SMessageTagWidget::OnRemoveTag(TSharedPtr<FMessageTagNode> InTagNode)
{
	if (InTagNode.IsValid())
	{
		for (int32 ContainerIdx = 0; ContainerIdx < TagContainers.Num(); ++ContainerIdx)
		{
			FMessageTagContainer* Container = TagContainers[ContainerIdx].TagContainer;
			Container->RemoveTag(InTagNode->GetCompleteTag());
		}

		OnTagChanged.ExecuteIfBound();
	}
}

void SMessageTagWidget::OnSearchForReferences(TSharedPtr<FMessageTagNode> InTagNode)
{
	extern void MesageTagsEditor_SearchMessageReferences(const TArray<FAssetIdentifier>& AssetIdentifiers);

	if (InTagNode.IsValid())
	{
		TArray<FAssetIdentifier> AssetIdentifiers;
		AssetIdentifiers.Emplace(FAssetIdentifier(FMessageTag::StaticStruct(), InTagNode->GetCompleteTagName()));
		MesageTagsEditor_SearchMessageReferences(AssetIdentifiers);
	}
}

void SMessageTagWidget::OnSearchMessage(TSharedPtr<FMessageTagNode> InTagNode)
{
	if (InTagNode.IsValid() && InTagNode->IsExplicitTag())
	{
		extern void MesageTagsEditor_FindMessageInBlueprints(const FString& MessageKey, class UBlueprint* Blueprint = nullptr);
		MesageTagsEditor_FindMessageInBlueprints(InTagNode->GetCompleteTagString());
	}
}

void SMessageTagWidget::SetTagTreeItemExpansion(bool bExpand)
{
	TArray<TSharedPtr<FMessageTagNode>> TagArray;
	UMessageTagsManager::Get().GetFilteredMessageRootTags(TEXT(""), TagArray);
	for (int32 TagIdx = 0; TagIdx < TagArray.Num(); ++TagIdx)
	{
		SetTagNodeItemExpansion(TagArray[TagIdx], bExpand);
	}
	
}

void SMessageTagWidget::SetTagNodeItemExpansion(TSharedPtr<FMessageTagNode> Node, bool bExpand)
{
	if (Node.IsValid() && TagTreeWidget.IsValid())
	{
		TagTreeWidget->SetItemExpansion(Node, bExpand);

		const TArray<TSharedPtr<FMessageTagNode>>& ChildTags = Node->GetChildTagNodes();
		for (int32 ChildIdx = 0; ChildIdx < ChildTags.Num(); ++ChildIdx)
		{
			SetTagNodeItemExpansion(ChildTags[ChildIdx], bExpand);
		}
	}
}

void SMessageTagWidget::LoadSettings()
{
	MigrateSettings();

	TArray<TSharedPtr<FMessageTagNode>> TagArray;
	UMessageTagsManager::Get().GetFilteredMessageRootTags(TEXT(""), TagArray);
	for (int32 TagIdx = 0; TagIdx < TagArray.Num(); ++TagIdx)
	{
		LoadTagNodeItemExpansion(TagArray[TagIdx] );
	}
}

const FString& SMessageTagWidget::GetMessageTagsEditorStateIni()
{
	static FString Filename;

	if (Filename.Len() == 0)
	{
		Filename = FString::Printf(TEXT("%s%s/MessageTagsEditorState.ini"), *FPaths::GeneratedConfigDir(), ANSI_TO_TCHAR(FPlatformProperties::PlatformName()));
		FPaths::MakeStandardFilename(Filename);
	}

	return Filename;
}

void SMessageTagWidget::MigrateSettings()
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

void SMessageTagWidget::SetDefaultTagNodeItemExpansion(TSharedPtr<FMessageTagNode> Node)
{
	if ( Node.IsValid() && TagTreeWidget.IsValid() )
	{
		bool bExpanded = false;

		if ( IsTagChecked(Node) == ECheckBoxState::Checked )
		{
			bExpanded = true;
		}
		TagTreeWidget->SetItemExpansion(Node, bExpanded);

		const TArray<TSharedPtr<FMessageTagNode>>& ChildTags = Node->GetChildTagNodes();
		for ( int32 ChildIdx = 0; ChildIdx < ChildTags.Num(); ++ChildIdx )
		{
			SetDefaultTagNodeItemExpansion(ChildTags[ChildIdx]);
		}
	}
}

void SMessageTagWidget::LoadTagNodeItemExpansion(TSharedPtr<FMessageTagNode> Node)
{
	if (Node.IsValid() && TagTreeWidget.IsValid())
	{
		bool bExpanded = false;

		if (GConfig->GetBool(*SettingsIniSection, *(TagContainerName + Node->GetCompleteTagString() + TEXT(".Expanded")), bExpanded, GEditorPerProjectIni))
		{
			TagTreeWidget->SetItemExpansion( Node, bExpanded );
		}
		else if( IsTagChecked( Node ) == ECheckBoxState::Checked ) // If we have no save data but its ticked then we probably lost our settings so we shall expand it
		{
			TagTreeWidget->SetItemExpansion( Node, true );
		}

		const TArray<TSharedPtr<FMessageTagNode>>& ChildTags = Node->GetChildTagNodes();
		for (int32 ChildIdx = 0; ChildIdx < ChildTags.Num(); ++ChildIdx)
		{
			LoadTagNodeItemExpansion(ChildTags[ChildIdx]);
		}
	}
}

void SMessageTagWidget::OnExpansionChanged(TSharedPtr<FMessageTagNode> InItem, bool bIsExpanded)
{
	// Save the new expansion setting to ini file
	GConfig->SetBool(*SettingsIniSection, *(TagContainerName + InItem->GetCompleteTagString() + TEXT(".Expanded")), bIsExpanded, GEditorPerProjectIni);
}

void SMessageTagWidget::SetContainer(FMessageTagContainer* OriginalContainer, FMessageTagContainer* EditedContainer, UObject* OwnerObj)
{
	if (PropertyHandle.IsValid() && bMultiSelect)
	{
		// Case for a tag container 
		PropertyHandle->SetValueFromFormattedString(EditedContainer->ToString());
	}
	else if (PropertyHandle.IsValid() && !bMultiSelect)
	{
		// Case for a single Tag		
		FString FormattedString = TEXT("(TagName=\"");
		FormattedString += EditedContainer->First().GetTagName().ToString();
		FormattedString += TEXT("\")");
		PropertyHandle->SetValueFromFormattedString(FormattedString);
	}
	else
	{
		// Not sure if we should get here, means the property handle hasnt been setup which could be right or wrong.
		if (OwnerObj)
		{
			OwnerObj->PreEditChange(PropertyHandle.IsValid() ? PropertyHandle->GetProperty() : nullptr);
		}

		*OriginalContainer = *EditedContainer;

		if (OwnerObj)
		{
			OwnerObj->PostEditChange();
		}
	}	

	if (!PropertyHandle.IsValid())
	{
		OnTagChanged.ExecuteIfBound();
	}
}

void SMessageTagWidget::OnMessageTagAdded(const FString& TagName, const FString& TagComment, const FName& TagSource)
{
	UMessageTagsManager& Manager = UMessageTagsManager::Get();

	RefreshTags();
	TagTreeWidget->RequestTreeRefresh();

	if (!EnumHasAllFlags(MessageTagUIMode, EMessageTagUIMode::ManagementMode))
	{
		TSharedPtr<FMessageTagNode> TagNode = Manager.FindTagNode(FName(*TagName));
		if (TagNode.IsValid())
		{
			OnTagChecked(TagNode);
		}

		// Filter on the new tag
		SearchTagBox->SetText(FText::FromString(TagName));

		// Close the Add New Tag UI
		bAddTagSectionExpanded = false;
	}
}

void SMessageTagWidget::RefreshTags()
{
	UMessageTagsManager& Manager = UMessageTagsManager::Get();
	Manager.GetFilteredMessageRootTags(RootFilterString, TagItems);

	if (bRestrictedTags)
	{
		// We only want to show the restricted message tags
		for (int32 Idx = TagItems.Num() - 1; Idx >= 0; --Idx)
		{
			if (!TagItems[Idx]->IsRestrictedMessageTag())
			{
				TagItems.RemoveAtSwap(Idx);
			}
		}
	}

#if 0
	if (Manager.OnFilterMessageTag.IsBound())
	{
		for (int32 Idx = TagItems.Num() - 1; Idx >= 0; --Idx)
		{
			bool DelegateShouldHide = false;
			FMessageTagSource* Source = Manager.FindTagSource(TagItems[Idx]->SourceName);
			Manager.OnFilterMessageTag.Broadcast(UMessageTagsManager::FFilterMessageTagContext(RootFilterString, TagItems[Idx], Source, PropertyHandle), DelegateShouldHide);
			if (DelegateShouldHide)
			{
				TagItems.RemoveAtSwap(Idx);
			}
		}
	}
#endif

	FilterTagTree();
}

EVisibility SMessageTagWidget::DetermineExpandableUIVisibility() const
{
	UMessageTagsManager& Manager = UMessageTagsManager::Get();

	if ( !Manager.ShouldImportTagsFromINI() )
	{
		// If we can't support adding tags from INI files, we should never see this widget
		return EVisibility::Collapsed;
	}

	return EVisibility::Visible;
}

EVisibility SMessageTagWidget::DetermineAddNewSourceExpandableUIVisibility() const
{
	if (bRestrictedTags)
	{
		return EVisibility::Collapsed;
	}

	return DetermineExpandableUIVisibility();
}

EVisibility SMessageTagWidget::DetermineAddNewTagWidgetVisibility() const
{
	UMessageTagsManager& Manager = UMessageTagsManager::Get();

	if ( !Manager.ShouldImportTagsFromINI() || !bAddTagSectionExpanded || bRestrictedTags )
	{
		// If we can't support adding tags from INI files, we should never see this widget
		return EVisibility::Collapsed;
	}

	return EVisibility::Visible;
}

EVisibility SMessageTagWidget::DetermineAddNewRestrictedTagWidgetVisibility() const
{
	UMessageTagsManager& Manager = UMessageTagsManager::Get();

	if (!Manager.ShouldImportTagsFromINI() || !bAddTagSectionExpanded || !bRestrictedTags)
	{
		// If we can't support adding tags from INI files, we should never see this widget
		return EVisibility::Collapsed;
	}

	return EVisibility::Visible;
}

EVisibility SMessageTagWidget::DetermineAddNewSourceWidgetVisibility() const
{
	UMessageTagsManager& Manager = UMessageTagsManager::Get();

	if (!Manager.ShouldImportTagsFromINI() || !bAddSourceSectionExpanded || bRestrictedTags)
	{
		// If we can't support adding tags from INI files, we should never see this widget
		return EVisibility::Collapsed;
	}

	return EVisibility::Visible;
}

EVisibility SMessageTagWidget::DetermineAddNewSubTagWidgetVisibility(TSharedPtr<FMessageTagNode> Node) const
{
	EVisibility LocalVisibility = DetermineExpandableUIVisibility();
	if (LocalVisibility != EVisibility::Visible)
	{
		return LocalVisibility;
	}

	// We do not allow you to add child tags under a conflict
	if (Node->bNodeHasConflict || Node->bAncestorHasConflict)
	{
		return EVisibility::Hidden;
	}

	// show if we're dealing with restricted tags exclusively or restricted tags that allow non-restricted children
	if (Node->GetAllowNonRestrictedChildren() || bRestrictedTags)
	{
		return EVisibility::Visible;
	}

	return EVisibility::Hidden;
}

EVisibility SMessageTagWidget::DetermineClearSelectionVisibility() const
{
	return (bShowClearAll && CanSelectTags()) ? EVisibility::Visible : EVisibility::Collapsed;
}

bool SMessageTagWidget::CanSelectTags() const
{
	return !bReadOnly && EnumHasAnyFlags(MessageTagUIMode, EMessageTagUIMode::SelectionMode);
}

bool SMessageTagWidget::CanSelectThisTags(TSharedPtr<FMessageTagNode> InTagNode) const
{
	return CanSelectTags() && (!EnumHasAnyFlags(MessageTagUIMode, EMessageTagUIMode::ExplicitSelMode) || (InTagNode.IsValid() && InTagNode->bIsExplicitTag));
}

int32 SMessageTagWidget::GetSwitcherIndex(TSharedPtr<FMessageTagNode> InTagNode) const
{
	return CanSelectThisTags(InTagNode) ? 1 : 0;
}

bool SMessageTagWidget::IsAddingNewTag() const
{
	return AddNewTagWidget.IsValid() && AddNewTagWidget->IsAddingNewTag();
}

ECheckBoxState SMessageTagWidget::GetAddTagSectionExpansionState() const
{
	return bAddTagSectionExpanded ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SMessageTagWidget::OnAddTagSectionExpansionStateChanged(ECheckBoxState NewState)
{
	bAddTagSectionExpanded = NewState == ECheckBoxState::Checked;
}

ECheckBoxState SMessageTagWidget::GetAddSourceSectionExpansionState() const
{
	return bAddSourceSectionExpanded ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SMessageTagWidget::OnAddSourceSectionExpansionStateChanged(ECheckBoxState NewState)
{
	bAddSourceSectionExpanded = NewState == ECheckBoxState::Checked;
}

void SMessageTagWidget::RefreshOnNextTick()
{
	bDelayRefresh = true;
}

void SMessageTagWidget::OnMessageTagRenamed(FString OldTagName, FString NewTagName)
{
	OnTagChanged.ExecuteIfBound();
}

void SMessageTagWidget::OpenRenameMessageTagDialog(TSharedPtr<FMessageTagNode> MessageTagNode, bool bAllowFullEdit) const
{
	TSharedRef<SWindow> RenameTagWindow =
		SNew(SWindow)
		.Title(LOCTEXT("RenameTagWindowTitle", "Rename Message Tag"))
		.ClientSize(FVector2D(320.0f, 110.0f))
		.SizingRule(ESizingRule::Autosized)
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	TSharedRef<SRenameMessageTagDialog> RenameTagDialog =
		SNew(SRenameMessageTagDialog)
		.bAllowFullEdit(bAllowFullEdit)
		.MessageTagNode(MessageTagNode)
		.OnMessageTagRenamed(const_cast<SMessageTagWidget*>(this), &SMessageTagWidget::OnMessageTagRenamed);

	RenameTagWindow->SetContent(RenameTagDialog);

	IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
	if (MainFrameModule.GetParentWindow().IsValid())
	{
		FSlateApplication::Get().AddModalWindow(RenameTagWindow, MainFrameModule.GetParentWindow().ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddModalWindow(RenameTagWindow, FSlateApplication::Get().FindBestParentWindowForDialogs(nullptr));
	}
}

TSharedPtr<SWidget> SMessageTagWidget::GetWidgetToFocusOnOpen()
{
	return SearchTagBox;
}

void SMessageTagWidget::VerifyAssetTagValidity()
{
	UMessageTagsManager& TagsManager = UMessageTagsManager::Get();

	// Find and remove any tags on the asset that are no longer in the library
	for (int32 ContainerIdx = 0; ContainerIdx < TagContainers.Num(); ++ContainerIdx)
	{
		UObject* OwnerObj = TagContainers[ContainerIdx].TagContainerOwner.Get();
		FMessageTagContainer* Container = TagContainers[ContainerIdx].TagContainer;

		if (Container)
		{
			FMessageTagContainer EditableContainer = *Container;

			// Use a set instead of a container so we can find and remove None tags
			TSet<FMessageTag> InvalidTags;

			for (auto It = Container->CreateConstIterator(); It; ++It)
			{
				FMessageTag TagToCheck = *It;

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
					EditableContainer.RemoveTag(*InvalidIter);
					InvalidTagNames += InvalidIter->ToString() + TEXT("\n");
				}
				SetContainer(Container, &EditableContainer, OwnerObj);

				FFormatNamedArguments Arguments;
				Arguments.Add(TEXT("Objects"), FText::FromString(InvalidTagNames));
				FText DialogText = FText::Format(LOCTEXT("MessageTagWidget_InvalidTags", "Invalid Tags that have been removed: \n\n{Objects}"), Arguments);
				FText DialogTitle = LOCTEXT("MessageTagWidget_Warning", "Warning");
#if UE_5_03_OR_LATER
				FMessageDialog::Open(EAppMsgType::Ok, DialogText, DialogTitle);
#else
				FMessageDialog::Open(EAppMsgType::Ok, DialogText, &DialogTitle);
#endif
			}
		}
	}
}
#if 0
void SMessageTagWidget::VerifyAssetTagValidity()
{
	FMessageTagContainer LibraryTags;

	// Create a set that is the library of all valid tags
	TArray<TSharedPtr<FMessageTagNode>> NodeStack;

	UMessageTagsManager& TagsManager = UMessageTagsManager::Get();

	TagsManager.GetFilteredMessageRootTags(TEXT(""), NodeStack);

	while (NodeStack.Num() > 0)
	{
		TSharedPtr<FMessageTagNode> CurNode = NodeStack.Pop();
		if (CurNode.IsValid())
		{
			LibraryTags.AddTag(CurNode->GetCompleteTag());
			NodeStack.Append(CurNode->GetChildTagNodes());
		}
	}

	// Find and remove any tags on the asset that are no longer in the library
	for (int32 ContainerIdx = 0; ContainerIdx < TagContainers.Num(); ++ContainerIdx)
	{
		UObject* OwnerObj = TagContainers[ContainerIdx].TagContainerOwner.Get();
		FMessageTagContainer* Container = TagContainers[ContainerIdx].TagContainer;

		if (Container)
		{
			FMessageTagContainer EditableContainer = *Container;

			// Use a set instead of a container so we can find and remove None tags
			TSet<FMessageTag> InvalidTags;

			for (auto It = Container->CreateConstIterator(); It; ++It)
			{
				FMessageTag TagToCheck = *It;

				// Check redirectors, these will get fixed on load time
				UMessageTagsManager::Get().RedirectSingleMessageTag(TagToCheck, nullptr);

				if (!LibraryTags.HasTagExact(TagToCheck))
				{
					InvalidTags.Add(*It);
				}
			}
			if (InvalidTags.Num() > 0)
			{
				FString InvalidTagNames;

				for (auto InvalidIter = InvalidTags.CreateConstIterator(); InvalidIter; ++InvalidIter)
				{
					EditableContainer.RemoveTag(*InvalidIter);
					InvalidTagNames += InvalidIter->ToString() + TEXT("\n");
				}
				SetContainer(Container, &EditableContainer, OwnerObj);

				FFormatNamedArguments Arguments;
				Arguments.Add(TEXT("Objects"), FText::FromString(InvalidTagNames));
				FText DialogText = FText::Format(LOCTEXT("MessageTagWidget_InvalidTags", "Invalid Tags that have been removed: \n\n{Objects}"), Arguments);
				FText DialogTitle = LOCTEXT("MessageTagWidget_Warning", "Warning");
				FMessageDialog::Open(EAppMsgType::Ok, DialogText, &DialogTitle);
			}
		}
	}
}

#endif
#undef LOCTEXT_NAMESPACE
