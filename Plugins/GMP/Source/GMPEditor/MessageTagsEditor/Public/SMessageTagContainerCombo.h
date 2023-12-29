// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Input/Reply.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "MessageTagContainer.h"
#include "EditorUndoClient.h"
#include "SMessageTagChip.h"

#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_NEWER_THAN(5, 0, 0)

class IPropertyHandle;
class SMenuAnchor;
class ITableRow;
class STableViewBase;
class SMessageTagPicker;
class SComboButton;

/**
 * Widget for editing a Message Tag Container.
 */
class SMessageTagContainerCombo : public SCompoundWidget, public FEditorUndoClient
{
	SLATE_DECLARE_WIDGET(SMessageTagContainerCombo, SCompoundWidget)
	
public:

	DECLARE_DELEGATE_OneParam(FOnTagContainerChanged, const FMessageTagContainer& /*TagContainer*/)

	SLATE_BEGIN_ARGS(SMessageTagContainerCombo)
		: _Filter()
		, _ReadOnly(false)
		, _EnableNavigation(false)
		, _PropertyHandle(nullptr)
	{}
		// Comma delimited string of tag root names to filter by
		SLATE_ARGUMENT(FString, Filter)

		// The name that will be used for the picker settings file
		SLATE_ARGUMENT(FString, SettingsName)

		// Flag to set if the list is read only
		SLATE_ARGUMENT(bool, ReadOnly)

		// If true, allow button navigation behavior
		SLATE_ARGUMENT(bool, EnableNavigation)

		// Tag container to edit
		SLATE_ATTRIBUTE(FMessageTagContainer, TagContainer)

		// If set, the tag container is read from the property, and the property is changed when tag container is edited. 
		SLATE_ARGUMENT(TSharedPtr<IPropertyHandle>, PropertyHandle)

		// Callback for when button body is pressed with LMB+Ctrl
		SLATE_EVENT(SMessageTagChip::FOnNavigate, OnNavigate)

		// Callback for when button body is pressed with RMB
		SLATE_EVENT(SMessageTagChip::FOnMenu, OnMenu)

		// Called when a tag container changes
		SLATE_EVENT(FOnTagContainerChanged, OnTagContainerChanged)
	SLATE_END_ARGS();

	MESSAGETAGSEDITOR_API SMessageTagContainerCombo();
	MESSAGETAGSEDITOR_API virtual ~SMessageTagContainerCombo() override;

	MESSAGETAGSEDITOR_API void Construct(const FArguments& InArgs);

protected:
	//~ Begin FEditorUndoClient Interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	//~ End FEditorUndoClient Interface

private:

	struct FEditableItem
	{
		FEditableItem() = default;
		FEditableItem(const FMessageTag InTag, const int InCount = 1)
			: Tag(InTag)
			, Count(InCount)
		{
		}
		
		FMessageTag Tag;
		int32 Count = 0;
		bool bMultipleValues = false;
	};

	bool IsValueEnabled() const;
	void RefreshTagContainers();
	TSharedRef<ITableRow> MakeTagListViewRow(TSharedPtr<FEditableItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnMenuOpenChanged(const bool bOpen);
	TSharedRef<SWidget> OnGetMenuContent();
	
	FReply OnTagMenu(const FPointerEvent& MouseEvent, const FMessageTag MessageTag);
	FReply OnEditClicked(const FMessageTag TagToHilight);
	FReply OnClearAllClicked();
	FReply OnClearTagClicked(const FMessageTag TagToClear);
	void OnTagChanged(const TArray<FMessageTagContainer>& TagContainers);
	void OnClearAll();
	void OnCopyTag(const FMessageTag TagToCopy) const;
	void OnPasteTag();
	bool CanPaste() const;
	void OnSearchForAnyReferences() const;
	
	TSlateAttribute<FMessageTagContainer> TagContainerAttribute;
	TArray<FMessageTagContainer> CachedTagContainers;
	TArray<TSharedPtr<FEditableItem>> TagsToEdit;
	TSharedPtr<SListView<TSharedPtr<FEditableItem>>> TagListView;
	
	FString Filter;
	FString SettingsName;
	bool bIsReadOnly = false;
	bool bRegisteredForUndo = false;
	FOnTagContainerChanged OnTagContainerChanged;
	TSharedPtr<IPropertyHandle> PropertyHandle;
	TSharedPtr<SMenuAnchor> MenuAnchor;
	TSharedPtr<SComboButton> ComboButton;
	TSharedPtr<SMessageTagPicker> TagPicker;
	FMessageTag TagToHilight;
};
#endif
