// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Input/Reply.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "MessageTagContainer.h"
#include "SMessageTagChip.h"

class IPropertyHandle;
class SMenuAnchor;
class SButton;
class SComboButton;
class SMessageTagPicker;

/**
 * Widget for editing a Message Tag.
 */
class SMessageTagCombo : public SCompoundWidget
{
	SLATE_DECLARE_WIDGET(SMessageTagCombo, SCompoundWidget)

public:

	DECLARE_DELEGATE_OneParam(FOnTagChanged, const FMessageTag /*Tag*/)

	SLATE_BEGIN_ARGS(SMessageTagCombo)
		: _Filter()
		, _ReadOnly(false)
		, _EnableNavigation(false)
		, _PropertyHandle(nullptr)
	{}
		// Comma delimited string of tag root names to filter by
		SLATE_ARGUMENT(FString, Filter)

		// The name that will be used for the tag picker settings file
		SLATE_ARGUMENT(FString, SettingsName)

		// Flag to set if the list is read only
		SLATE_ARGUMENT(bool, ReadOnly)

		// If true, allow button navigation behavior
		SLATE_ARGUMENT(bool, EnableNavigation)

		// Tags to edit
		SLATE_ATTRIBUTE(FMessageTag, Tag)

		// If set, the tag is read from the property, and the property is changed when tag is edited. 
		SLATE_ARGUMENT(TSharedPtr<IPropertyHandle>, PropertyHandle)

		// Callback for when button body is pressed with LMB+Ctrl
		SLATE_EVENT(SMessageTagChip::FOnNavigate, OnNavigate)

		// Callback for when button body is pressed with RMB
		SLATE_EVENT(SMessageTagChip::FOnMenu, OnMenu)

		// Called when a tag status changes
		SLATE_EVENT(FOnTagChanged, OnTagChanged)
	SLATE_END_ARGS();

	MESSAGETAGSEDITOR_API SMessageTagCombo();

	MESSAGETAGSEDITOR_API void Construct(const FArguments& InArgs);

private:

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	
	bool ShowClearButton() const;
	FText GetText() const;
	bool IsValueEnabled() const;
	FText GetToolTipText() const;
	bool IsSelected() const;
	FReply OnClearPressed();
	void OnMenuOpenChanged(const bool bOpen) const;
	TSharedRef<SWidget> OnGetMenuContent();
	FMessageTag GetCommonTag() const;
	FReply OnTagMenu(const FPointerEvent& MouseEvent);
	FReply OnEditTag() const;
	void RefreshTagsFromProperty();
	void OnTagSelected(const TArray<FMessageTagContainer>& TagContainers);
	void OnClearTag();
	void OnCopyTag(const FMessageTag TagToCopy) const;
	void OnPasteTag();
	bool CanPaste() const;
#if UE_5_00_OR_LATER
	TSlateAttribute<FMessageTag> TagAttribute;
#else
	TAttribute<FMessageTag> TagAttribute;
#endif
	TArray<FMessageTag> TagsFromProperty;
	bool bHasMultipleValues = false;
	bool bIsReadOnly = false;
	FString Filter;
	FString SettingsName;
	FOnTagChanged OnTagChanged;
	TSharedPtr<IPropertyHandle> PropertyHandle;
	TSharedPtr<SMenuAnchor> MenuAnchor;
	TSharedPtr<SComboButton> ComboButton;
	TSharedPtr<SMessageTagPicker> TagPicker;
};
