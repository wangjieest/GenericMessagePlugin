// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPropertyTypeCustomization.h"

struct FGameplayTag;
class IPropertyHandle;
class FDetailWidgetRow;
class IDetailChildrenBuilder;
class IPropertyTypeCustomizationUtils;

/** Customization for the message tag container struct */
class FMessageTagContainerCustomization : public IPropertyTypeCustomization
{
public:
	FMessageTagContainerCustomization();

	/** Overridden to show an edit button to launch the message tag editor */
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	
	/** Overridden to do nothing */
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override {}

private:
	/** Cached property handle */
	TSharedPtr<IPropertyHandle> StructPropertyHandle;

	void OnPasteTag() const;
	bool CanPasteTag() const;
};

