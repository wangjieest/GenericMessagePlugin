// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Editor/PropertyEditor/Public/IPropertyTypeCustomization.h"
#include "SMessageTagWidget.h"
#include "EditorUndoClient.h"

class IPropertyHandle;

/** Customization for the message tag struct */
class FMessageTagCustomization : public IPropertyTypeCustomization, public FEditorUndoClient
{
public:

	~FMessageTagCustomization();

	/** Overridden to show an edit button to launch the message tag editor */
	virtual void CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	
	/** Overridden to do nothing */
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override {}

	//~ Begin FEditorUndoClient Interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	//~ End FEditorUndoClient Interface

private:

	/** Updates the selected tag*/
	void OnTagChanged();

	/** Updates the selected tag*/
	void OnPropertyValueChanged();

	/** Build Editable Container */
	void BuildEditableContainerList();

	/** Callback function to create content for the combo button. */
	TSharedRef<SWidget> GetListContent();

	void OnMessageTagListMenuOpenStateChanged(bool bIsOpened);

	/** Returns Tag name currently selected*/
	FText SelectedTag() const;

	/** Cached property handle */
	TSharedPtr<IPropertyHandle> StructPropertyHandle;

	/** Tag Container used for the MessageTagWidget. */
	TSharedPtr<FMessageTagContainer> TagContainer;

	/** Editable Container for holding our tag */
	TArray<SMessageTagWidget::FEditableMessageTagContainerDatum> EditableContainers;

	/** Tag name selected*/
	FString TagName;

	void OnTagDoubleClicked();
	void OnSearchForReferences();

	EVisibility GetVisibilityForTagTextBlockWidget(bool ForTextWidget) const;

	TSharedPtr<class SComboButton> EditButton;

	TWeakPtr<class SMessageTagWidget> LastTagWidget;
};

/** Customization for FGameplayTagCreationWidgetHelper showing an add tag button */
class FMessageTagCreationWidgetHelperDetails : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	/** IPropertyTypeCustomization interface */
	virtual void CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	TSharedPtr<class SMessageTagWidget> TagWidget;
};