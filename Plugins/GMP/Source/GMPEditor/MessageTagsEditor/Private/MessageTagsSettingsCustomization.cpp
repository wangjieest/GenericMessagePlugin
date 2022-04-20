// Copyright Epic Games, Inc. All Rights Reserved.

#include "MessageTagsSettingsCustomization.h"
#include "MessageTagsSettings.h"
#include "MessageTagsModule.h"
#include "PropertyHandle.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"

#define LOCTEXT_NAMESPACE "FMessageTagsSettingsCustomization"

TSharedRef<IDetailCustomization> FMessageTagsSettingsCustomization::MakeInstance()
{
	return MakeShareable( new FMessageTagsSettingsCustomization() );
}

FMessageTagsSettingsCustomization::FMessageTagsSettingsCustomization()
{
	IMessageTagsModule::OnTagSettingsChanged.AddRaw(this, &FMessageTagsSettingsCustomization::OnTagTreeChanged);
}

FMessageTagsSettingsCustomization::~FMessageTagsSettingsCustomization()
{
	IMessageTagsModule::OnTagSettingsChanged.RemoveAll(this);
}

void FMessageTagsSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	const float MaxPropertyWidth = 480.0f;
	const float MaxPropertyHeight = 240.0f;

	IDetailCategoryBuilder& MessageTagsCategory = DetailLayout.EditCategory("MessageTags");
	{
		TArray<TSharedRef<IPropertyHandle>> MessageTagsProperties;
		MessageTagsCategory.GetDefaultProperties(MessageTagsProperties, true, true);

		TSharedPtr<IPropertyHandle> TagListProperty = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UMessageTagsList, MessageTagList), UMessageTagsList::StaticClass());
		TagListProperty->MarkHiddenByCustomization();

		for (TSharedPtr<IPropertyHandle> Property : MessageTagsProperties)
		{
			if (Property->GetProperty() != TagListProperty->GetProperty())
			{
				MessageTagsCategory.AddProperty(Property);
			}
			else
			{
				// Create a custom widget for the tag list

				MessageTagsCategory.AddCustomRow(TagListProperty->GetPropertyDisplayName(), false)
				.NameContent()
				[
					TagListProperty->CreatePropertyNameWidget()
				]
				.ValueContent()
				.MaxDesiredWidth(MaxPropertyWidth)
				[
					SAssignNew(TagWidget, SMessageTagWidget, TArray<SMessageTagWidget::FEditableMessageTagContainerDatum>())
					.Filter(TEXT(""))
					.MultiSelect(false)
					.MessageTagUIMode(EMessageTagUIMode::ManagementMode)
					.MaxHeight(MaxPropertyHeight)
					.OnTagChanged(this, &FMessageTagsSettingsCustomization::OnTagChanged)
					.RestrictedTags(false)
				];
			}
		}
	}

	IDetailCategoryBuilder& AdvancedMessageTagsCategory = DetailLayout.EditCategory("Advanced Message Tags");
	{
		TArray<TSharedRef<IPropertyHandle>> MessageTagsProperties;
		AdvancedMessageTagsCategory.GetDefaultProperties(MessageTagsProperties, true, true);

		TSharedPtr<IPropertyHandle> RestrictedTagListProperty = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UMessageTagsSettings, RestrictedTagList));
		RestrictedTagListProperty->MarkHiddenByCustomization();

		for (TSharedPtr<IPropertyHandle> Property : MessageTagsProperties)
		{
			if (Property->GetProperty() == RestrictedTagListProperty->GetProperty())
			{
				// Create a custom widget for the restricted tag list

				AdvancedMessageTagsCategory.AddCustomRow(RestrictedTagListProperty->GetPropertyDisplayName(), true)
				.NameContent()
				[
					RestrictedTagListProperty->CreatePropertyNameWidget()
				]
				.ValueContent()
				.MaxDesiredWidth(MaxPropertyWidth)
				[
					SAssignNew(RestrictedTagWidget, SMessageTagWidget, TArray<SMessageTagWidget::FEditableMessageTagContainerDatum>())
					.Filter(TEXT(""))
					.MultiSelect(false)
					.MessageTagUIMode(EMessageTagUIMode::ManagementMode)
					.MaxHeight(MaxPropertyHeight)
					.OnTagChanged(this, &FMessageTagsSettingsCustomization::OnTagChanged)
					.RestrictedTags(true)
				];
			}
			else
			{
				AdvancedMessageTagsCategory.AddProperty(Property);
			}
		}
	}
}

void FMessageTagsSettingsCustomization::OnTagChanged()
{
	if (TagWidget.IsValid())
	{
		TagWidget->RefreshTags();
	}

	if (RestrictedTagWidget.IsValid())
	{
		RestrictedTagWidget->RefreshTags();
	}
}

void FMessageTagsSettingsCustomization::OnTagTreeChanged()
{
	if (TagWidget.IsValid())
	{
		TagWidget->RefreshOnNextTick();
	}

	if (RestrictedTagWidget.IsValid())
	{
		RestrictedTagWidget->RefreshOnNextTick();
	}
}

#undef LOCTEXT_NAMESPACE
