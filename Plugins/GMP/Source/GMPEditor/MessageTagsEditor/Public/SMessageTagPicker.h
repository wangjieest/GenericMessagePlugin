// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SlateFwd.h"
#include "UObject/Object.h"
#include "Layout/Visibility.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Input/Reply.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"
#include "MessageTagsManager.h"
#include "EditorUndoClient.h"
#include "Misc/EngineVersionComparison.h"

class IPropertyHandle;
class SComboButton;
class SAddNewMessageTagWidget;

/** Determines the behavior of the Message tag UI depending on where it's used */
enum class EMessageTagPickerMode : uint8
{
	SelectionMode,		// Allows selecting tags for Message tag containers.
	ManagementMode,		// Allows renaming/deleting tags, does not require Message tag containers.
	HybridMode			// Allows doing operations from both Selection and Management mode.
};

/** Widget allowing user to tag assets with Message tags */
class MESSAGETAGSEDITOR_API SMessageTagPicker : public SCompoundWidget, public FSelfRegisteringEditorUndoClient
{
public:

	/** Called when a tag status is changed */
	DECLARE_DELEGATE_OneParam(FOnTagChanged, const TArray<FMessageTagContainer>& /*TagContainers*/)

	/** Called on when tags might need refreshing (e.g. after undo/redo or when tags change). Use SetTagContainers() to set the new tags. */
	DECLARE_DELEGATE_OneParam(FOnRefreshTagContainers, SMessageTagPicker& /*TagPicker*/)
	
	enum class ETagFilterResult
	{
		IncludeTag,
		ExcludeTag
	};

	DECLARE_DELEGATE_RetVal_OneParam(ETagFilterResult, FOnFilterTag, const TSharedPtr<FMessageTagNode>&)

	SLATE_BEGIN_ARGS(SMessageTagPicker)
		: _Filter()
		, _SettingsName(TEXT(""))
		, _ReadOnly(false)
		, _MultiSelect(true)
		, _RestrictedTags(false)
		, _ShowMenuItems(false)
		, _PropertyHandle(nullptr)
		, _MessageTagPickerMode(EMessageTagPickerMode::SelectionMode)
		, _MaxHeight(260.0f)
		, _Padding(FMargin(2.0f))
	{}
		// Comma delimited string of tag root names to filter by
		SLATE_ARGUMENT(FString, Filter)

		// Optional filter function called when generating the tag list
		SLATE_EVENT(FOnFilterTag, OnFilterTag)

		// The name that will be used for the settings file
		SLATE_ARGUMENT(FString, SettingsName)

		// Flag to set if the list is read only
		SLATE_ARGUMENT(bool, ReadOnly)

		// If we can select multiple entries
		SLATE_ARGUMENT(bool, MultiSelect)

		// If we are dealing with restricted tags or regular Message tags
		SLATE_ARGUMENT(bool, RestrictedTags)

		// If set, wraps the picker in a menu builder and adds common menu commands.
		SLATE_ARGUMENT(bool, ShowMenuItems)  

		// Property handle to FMessageTag or FMessageTagContainer, used to get and modify the edited tags.
		SLATE_ARGUMENT(TSharedPtr<IPropertyHandle>, PropertyHandle)

		// Tags or tag containers to modify. If MultiSelect is false, the container will container single tags.
		// If PropertyHandle is set, the tag containers will be ignored.
		SLATE_ARGUMENT(TArray<FMessageTagContainer>, TagContainers)

		// Called when a tag status changes
		SLATE_EVENT(FOnTagChanged, OnTagChanged)

		// Determines behavior of the menu based on where it's used
		SLATE_ARGUMENT(EMessageTagPickerMode, MessageTagPickerMode)

		// Caps the height of the Message tag tree, 0.0 means uncapped.
		SLATE_ARGUMENT(float, MaxHeight)

		// Padding inside the picker widget
		SLATE_ARGUMENT(FMargin, Padding)

		// Called on when tags might need refreshing (e.g. after undo/redo or when tags change).
		SLATE_EVENT(FOnRefreshTagContainers, OnRefreshTagContainers)
	SLATE_END_ARGS()

	/**
	 * Given a property handle, try and enumerate the tag containers from within it (when dealing with a struct property of type FMessageTag or FMessageTagContainer).
	 * @return True if it was possible to enumerate containers (even if no containers were enumerated), or false otherwise.
	 */
	static bool EnumerateEditableTagContainersFromPropertyHandle(const TSharedRef<IPropertyHandle>& PropHandle, TFunctionRef<bool(const FMessageTagContainer&)> Callback);

	/**
	 * Given a property handle, try and extract the editable tag containers from within it (when dealing with a struct property of type FMessageTagContainer).
	 * @return True if it was possible to extract containers (even if no containers were extracted), or false otherwise.
	 */
	static bool GetEditableTagContainersFromPropertyHandle(const TSharedRef<IPropertyHandle>& PropHandle, TArray<FMessageTagContainer>& OutEditableContainers);

	/** Construct the actual widget */
	void Construct(const FArguments& InArgs);
	
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Updates the tag list when the filter text changes */
	void OnFilterTextChanged(const FText& InFilterText);

	/** Returns true if this TagNode has any children that match the current filter */
	bool FilterChildrenCheck(TSharedPtr<FMessageTagNode>& InItem) const;

	/** Child recursion helper for FilterChildrenCheck() */
	bool FilterChildrenCheckRecursive(TSharedPtr<FMessageTagNode>& InItem) const;

	/** Refreshes the tags that should be displayed by the widget */
	void RefreshTags();

	/** Forces the widget to refresh its tags on the next tick */
	void RefreshOnNextTick();

	/** Scrolls the view to specified tag. */
	void RequestScrollToView(const FMessageTag Tag);
	
	/** Gets the widget to focus once the menu opens. */
	TSharedPtr<SWidget> GetWidgetToFocusOnOpen();

	/** Sets tag containers to edit. */
	void SetTagContainers(TConstArrayView<FMessageTagContainer> TagContainers);
	
private:

	enum class EMessageTagAdd : uint8
	{
		Root,
		Child,
		Duplicate,
	};

	// FSelfRegisteringEditorUndoClient
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	// ~FSelfRegisteringEditorUndoClient

	/** Verify the tags are all valid and if not prompt the user. */
	void VerifyAssetTagValidity();

	/* Filters the tree view based on the current filter text. */
	void FilterTagTree();

	/* string that sets the section of the ini file to use for this class*/ 
	static const FString SettingsIniSection;

	/* Filename of ini file to store state of the UI */
	static const FString& GetMessageTagsEditorStateIni();

	/**
	 * Generate a row widget for the specified item node and table
	 * 
	 * @param InItem		Tag node to generate a row widget for
	 * @param OwnerTable	Table that owns the row
	 * 
	 * @return Generated row widget for the item node
	 */
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FMessageTagNode> InItem, const TSharedRef<STableViewBase>& OwnerTable);

	/**
	 * Get children nodes of the specified node
	 * 
	 * @param InItem		Node to get children of
	 * @param OutChildren	[OUT] Array of children nodes, if any
	 */
	void OnGetChildren(TSharedPtr<FMessageTagNode> InItem, TArray< TSharedPtr<FMessageTagNode> >& OutChildren);

	/**
	 * Called via delegate when the status of a check box in a row changes
	 * 
	 * @param NewCheckState	New check box state
	 * @param NodeChanged	Node that was checked/unchecked
	 */
	void OnTagCheckStatusChanged(ECheckBoxState NewCheckState, TSharedPtr<FMessageTagNode> NodeChanged);

	/**
	 * Called via delegate to determine the checkbox state of the specified node
	 * 
	 * @param Node	Node to find the checkbox state of
	 * 
	 * @return Checkbox state of the specified node
	 */
	ECheckBoxState IsTagChecked(TSharedPtr<FMessageTagNode> Node) const;

	/**
	 * @return true if the exact Tag provided is included in any of the tag containers the widget is editing.
	 */
	bool IsExactTagInCollection(TSharedPtr<FMessageTagNode> Node) const;

	/**
	 * Called via delegate when the status of the allow non-restricted children check box in a row changes
	 *
	 * @param NewCheckState	New check box state
	 * @param NodeChanged	Node that was checked/unchecked
	 */
	void OnAllowChildrenTagCheckStatusChanged(ECheckBoxState NewCheckState, TSharedPtr<FMessageTagNode> NodeChanged);

	/**
	 * Called via delegate to determine the non-restricted children checkbox state of the specified node
	 *
	 * @param Node	Node to find the non-restricted children checkbox state of
	 *
	 * @return Non-restricted children heckbox state of the specified node
	 */
	ECheckBoxState IsAllowChildrenTagChecked(TSharedPtr<FMessageTagNode> Node) const;

	/** Helper function to determine the visibility of the checkbox for allowing non-restricted children of restricted Message tags */
	EVisibility DetermineAllowChildrenVisible(TSharedPtr<FMessageTagNode> Node) const;

	/**
	 * Helper function called when the specified node is checked
	 * 
	 * @param NodeChecked	Node that was checked by the user
	 */
	void OnTagChecked(TSharedPtr<FMessageTagNode> NodeChecked);

	/**
	 * Helper function called when the specified node is unchecked
	 * 
	 * @param NodeUnchecked	Node that was unchecked by the user
	 */
	void OnTagUnchecked(TSharedPtr<FMessageTagNode> NodeUnchecked);

	/**
	 * Recursive function to uncheck all child tags
	 * 
	 * @param NodeUnchecked	Node that was unchecked by the user
	 * @param EditableContainer The container we are removing the tags from
	 */
	void UncheckChildren(TSharedPtr<FMessageTagNode> NodeUnchecked, FMessageTagContainer& EditableContainer);

	/**
	 * Called via delegate to determine the text colour of the specified node
	 *
	 * @param Node	Node to find the colour of
	 *
	 * @return Text colour of the specified node
	 */
	FSlateColor GetTagTextColour(TSharedPtr<FMessageTagNode> Node) const;

	/** Called when the user clicks the "Create and Manage Message Tags" button; Opens Message Tag manager window. */
	void OnManageTagsClicked(TSharedPtr<FMessageTagNode> Node, TSharedPtr<SComboButton> OwnerCombo);

	/** Called when the user clicks the "Clear All" button; Clears all tags */
	void OnClearAllClicked(TSharedPtr<SComboButton> OwnerCombo);

	/** Called when the user clicks the "Expand All" button; Expands the entire tag tree */
	void OnExpandAllClicked(TSharedPtr<SComboButton> OwnerCombo);

	/** Called when the user clicks the "Collapse All" button; Collapses the entire tag tree */
	void OnCollapseAllClicked(TSharedPtr<SComboButton> OwnerCombo);

	/**
	 * Helper function to set the expansion state of the tree widget
	 * 
	 * @param bExpand If true, expand the entire tree; Otherwise, collapse the entire tree
	 * @param bPersistExpansion If true, persist the expansion state.
	 */
	void SetTagTreeItemExpansion(bool bExpand, bool bPersistExpansion = false);

	/**
	 * Helper function to set the expansion state of a specific node
	 * 
	 * @param Node		Node to set the expansion state of
	 * @param bExpand	If true, expand the node; Otherwise, collapse the node
	 * @param bPersistExpansion If true, persist the expansion state.
	 */
	void SetTagNodeItemExpansion(TSharedPtr<FMessageTagNode> Node, bool bExpand, bool bPersistExpansion = false);

	/** Load settings for the tags*/
	void LoadSettings();

	/** Migrate settings */
	void MigrateSettings();

	/** Helper function to determine the visibility of the expandable UI controls */
	EVisibility DetermineExpandableUIVisibility() const;

	/** Helper function to determine the visibility of the Add New Tag widget */
	bool CanAddNewTag() const;

	/** Helper function to determine the visibility of the Add New Subtag widget */
	bool CanAddNewSubTag(TSharedPtr<FMessageTagNode> Node) const;

	/** Helper function to determine if tag can be modified */
	bool CanModifyTag(TSharedPtr<FMessageTagNode> Node) const;

	/** Recursive load function to go through all tags in the tree and set the expansion*/
	void LoadTagNodeItemExpansion(TSharedPtr<FMessageTagNode> Node );

	/** Recursive function to go through all tags in the tree and set the expansion to default*/
	void SetDefaultTagNodeItemExpansion(TSharedPtr<FMessageTagNode> Node);

	/** Expansion changed callback */
	void OnExpansionChanged(TSharedPtr<FMessageTagNode> InItem, bool bIsExpanded);

	/** Callback for when a new tag is added */
	void OnMessageTagAdded(const FString& TagName, const FString& TagComment, const FName& TagSource);

	/** Opens add tag modal dialog. */
	void OpenAddTagDialog(const EMessageTagAdd Mode, TSharedPtr<FMessageTagNode> InTagNode = TSharedPtr<FMessageTagNode>());

	/** Initializes the inline add tag widget and makes in visible. */
	void ShowInlineAddTagWidget(const EMessageTagAdd Mode, TSharedPtr<FMessageTagNode> InTagNode = TSharedPtr<FMessageTagNode>());

	FReply OnAddRootTagClicked();

	FText GetHighlightText() const;
	
	/** Creates a dropdown menu to provide additional functionality for tags (renaming, deletion, search for references, etc.) */
	TSharedRef<SWidget> MakeTagActionsMenu(TSharedPtr<FMessageTagNode> InTagNode, TSharedPtr<SComboButton> OwnerCombo, const bool bInShouldCloseWindowAfterMenuSelection);

	/** Creates settings menu content. */
	TSharedRef<SWidget> MakeSettingsMenu(TSharedPtr<SComboButton> OwnerCombo);

	void OnAddSubTag(TSharedPtr<FMessageTagNode> InTagNode, TSharedPtr<SComboButton> OwnerCombo);

	void OnDuplicateTag(TSharedPtr<FMessageTagNode> InTagNode, TSharedPtr<SComboButton> OwnerCombo);
	
	/** Attempts to rename the tag through a dialog box */
	void OnRenameTag(TSharedPtr<FMessageTagNode> InTagNode, TSharedPtr<SComboButton> OwnerCombo);

	/** Attempts to delete the specified tag */
	void OnDeleteTag(TSharedPtr<FMessageTagNode> InTagNode, TSharedPtr<SComboButton> OwnerCombo);

	/** Attempts to select the exact specified tag*/
	void OnSelectExactTag(TSharedPtr<FMessageTagNode> InTagNode, TSharedPtr<SComboButton> OwnerCombo);

	/** Attempts to unselect the specified tag, but not the children */
	void OnUnselectExactTag(TSharedPtr<FMessageTagNode> InTagNode, TSharedPtr<SComboButton> OwnerCombo);

	/** Searches for all references for the selected tag */
	void OnSearchForReferences(TSharedPtr<FMessageTagNode> InTagNode, TSharedPtr<SComboButton> OwnerCombo);
	void OnSearchMessage(TSharedPtr<FMessageTagNode> InTagNode);

	/** Copies individual tag's name to clipboard. */
	void OnCopyTagNameToClipboard(TSharedPtr<FMessageTagNode> InTagNode, TSharedPtr<SComboButton> OwnerCombo);
	
	/** Returns true if the user can select tags from the widget */
	bool CanSelectTags() const;

	/** Called when tags are selected. */
	void OnContainersChanged();

	/** Opens a dialog window to rename the selected tag */
	void OpenRenameMessageTagDialog(TSharedPtr<FMessageTagNode> MessageTagNode) const;

	/** Delegate that is fired when a tag is successfully renamed */
	void OnMessageTagRenamed(FString OldTagName, FString NewTagName);

	/** Populate tag items from the Message tags manager. */
	void GetFilteredMessageRootTags(const FString& InFilterString, TArray<TSharedPtr<FMessageTagNode>>& OutNodes) const;

	/** Called to create context menu for the tag tree items. */
	TSharedPtr<SWidget> OnTreeContextMenuOpening();

	/** Called when the tag tree selection changes. */
	void OnTreeSelectionChanged(TSharedPtr<FMessageTagNode> SelectedItem, ESelectInfo::Type SelectInfo);

	/** Called to handle key presses in the tag tree. */
	FReply OnTreeKeyDown(const FGeometry& Geometry, const FKeyEvent& Key);
	
	/* Holds the Name of this TagContainer used for saving out expansion settings */
	FString SettingsName;

	/* Filter string used during search box */
	FString FilterString;

	/** root filter (passed in on creation) */
	FString RootFilterString;

	/** User specified filter function. */
	FOnFilterTag TagFilter; 

	/* Flag to set if the list is read only*/
	bool bReadOnly = false;

	/* Flag to set if we can select multiple items form the list*/
	bool bMultiSelect = true;

	/** If true, refreshes tags on the next frame */
	bool bDelayRefresh = false;

	/** If true, this widget is displaying restricted tags; if false this widget displays regular Message tags. */
	bool bRestrictedTags = false;

	/** The maximum height of the Message tag tree. If 0, the height is unbound. */
	float MaxHeight = 260.0f;

	/* Array of all tags */
	TArray<TSharedPtr<FMessageTagNode>> TagItems;

	/* Array of tags filtered to be displayed in the TreeView */
	TArray<TSharedPtr<FMessageTagNode>> FilteredTagItems;

	/** Container widget holding the tag tree */
	TSharedPtr<SBorder> TagTreeContainerWidget;

	/** Tree widget showing the Message tag library */
	TSharedPtr<STreeView<TSharedPtr<FMessageTagNode>>> TagTreeWidget;

	/** Allows for the user to find a specific Message tag in the tree */
	TSharedPtr<SSearchBox> SearchTagBox;

	/** Property to edit */
	TSharedPtr<IPropertyHandle> PropertyHandle;

	/** Containers to modify, ignored if PropertyHandle is set. */
	TArray<FMessageTagContainer> TagContainers;

	/** Called when the Tag list changes*/
	FOnTagChanged OnTagChanged;

	/** Called on when tags might need refreshing (e.g. after undo/redo or when tags change). */
	FOnRefreshTagContainers OnRefreshTagContainers;

	/** Tag to scroll to, cleared after scroll is requested. */
	FMessageTag RequestedScrollToTag;
	
	/** Determines behavior of the widget */
	EMessageTagPickerMode MessageTagPickerMode = EMessageTagPickerMode::SelectionMode;

	/** Guard value used to prevent feedback loops in selection handling. */
	bool bInSelectionChanged = false;

	/** Guard value used define if expansion code should persist the expansion operations. */
	bool bPersistExpansionChange = true;

	/** Expanded items cached from settings */
	TSet<TSharedPtr<FMessageTagNode>> CachedExpandedItems;

	TSharedPtr<SAddNewMessageTagWidget> AddNewTagWidget;
	bool bNewTagWidgetVisible = false;
};


struct FMessageTagManagerWindowArgs
{
	FText Title;
	FString Filter; // Comma delimited string of tag root names to filter by
	FMessageTag HighlightedTag; // Tag to highlight when window is opened. 
	bool bRestrictedTags;
};

namespace UE{namespace MessageTags{namespace Editor
{
	MESSAGETAGSEDITOR_API TWeakPtr<SMessageTagPicker> OpenMessageTagManager(const FMessageTagManagerWindowArgs& Args);
	MESSAGETAGSEDITOR_API TSharedRef<SWidget> Create(const FMessageTagManagerWindowArgs& Args);
}}}