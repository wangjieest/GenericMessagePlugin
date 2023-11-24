// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPropertyTypeCustomization.h"
#include "MessageTagContainer.h"

struct EVisibility;

class IPropertyHandle;
class FDetailWidgetRow;
class IDetailChildrenBuilder;
class SMessageTagPicker;

/** Customization for the message tag struct */
class FMessageTagCustomization : public IPropertyTypeCustomization
{
public:

	FMessageTagCustomization();

	/** Overridden to show an edit button to launch the message tag editor */
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	
	/** Overridden to do nothing */
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override {}

private:

	/** Cached property handle */
	TSharedPtr<IPropertyHandle> StructPropertyHandle;

	/** Edited Tag */
	FMessageTag Tag;
};

/** Customization for FGameplayTagCreationWidgetHelper showing an add tag button */
class FMessageTagCreationWidgetHelperDetails : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	/** IPropertyTypeCustomization interface */
	virtual void CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

	TSharedPtr<SMessageTagPicker> TagWidget;
};