// Copyright Epic Games, Inc. All Rights Reserved.

#include "MessageTagCustomization.h"
#include "PropertyHandle.h"
#include "DetailWidgetRow.h"
#include "MessageTagsManager.h"
#include "IDetailChildrenBuilder.h"
#include "MessageTagsEditorModule.h"
#include "Widgets/Input/SHyperlink.h"
#include "SMessageTagCombo.h"
#include "SMessageTagPicker.h"

#define LOCTEXT_NAMESPACE "MessageTagCustomization"

//---------------------------------------------------------
// FMessageTagCustomizationnPublic
//---------------------------------------------------------

TSharedRef<IPropertyTypeCustomization> FMessageTagCustomizationPublic::MakeInstance()
{
	return MakeShareable(new FMessageTagCustomization);
}

// Deprecated version.
TSharedRef<IPropertyTypeCustomization> FMessageTagCustomizationPublic::MakeInstanceWithOptions(const FMessageTagCustomizationOptions& Options)
{
	return MakeShareable(new FMessageTagCustomization());
}

//---------------------------------------------------------
// FMessageTagCustomization
//---------------------------------------------------------

FMessageTagCustomization::FMessageTagCustomization()
{
}

void FMessageTagCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructPropertyHandle = InStructPropertyHandle;

	HeaderRow
	.NameContent()
	[
		StructPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SBox)
		.Padding(FMargin(0,2,0,1))
		[
			SNew(SMessageTagCombo)
			.PropertyHandle(StructPropertyHandle)
		]
	];
}

//---------------------------------------------------------
// FMessageTagCreationWidgetHelperDetails
//---------------------------------------------------------

TSharedRef<IPropertyTypeCustomization> FMessageTagCreationWidgetHelperDetails::MakeInstance()
{
	return MakeShareable(new FMessageTagCreationWidgetHelperDetails());
}

void FMessageTagCreationWidgetHelperDetails::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	HeaderRow.WholeRowContent()[ SNullWidget::NullWidget ];
}

void FMessageTagCreationWidgetHelperDetails::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	const FString FilterString = UMessageTagsManager::Get().GetCategoriesMetaFromPropertyHandle(StructPropertyHandle);
	constexpr float MaxPropertyWidth = 480.0f;
	constexpr float MaxPropertyHeight = 240.0f;

	StructBuilder.AddCustomRow(NSLOCTEXT("MessageTagReferenceHelperDetails", "NewTag", "NewTag"))
		.ValueContent()
		.MaxDesiredWidth(MaxPropertyWidth)
		[
			SAssignNew(TagWidget, SMessageTagPicker)
			.Filter(FilterString)
			.MultiSelect(false)
			.MessageTagPickerMode(EMessageTagPickerMode::ManagementMode)
			.MaxHeight(MaxPropertyHeight)
		];
}

#undef LOCTEXT_NAMESPACE
