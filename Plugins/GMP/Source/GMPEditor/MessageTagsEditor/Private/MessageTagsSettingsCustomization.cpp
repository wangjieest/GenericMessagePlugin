// Copyright Epic Games, Inc. All Rights Reserved.

#include "MessageTagsSettingsCustomization.h"
#include "MessageTagsSettings.h"
#include "MessageTagsModule.h"
#include "PropertyHandle.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"

#if UE_VERSION_NEWER_THAN(5, 0, 0)
#include "SMessageTagPicker.h"
#include "SAddNewMessageTagSourceWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "FMessageTagsSettingsCustomization"

TSharedRef<IDetailCustomization> FMessageTagsSettingsCustomization::MakeInstance()
{
	return MakeShareable( new FMessageTagsSettingsCustomization() );
}

FMessageTagsSettingsCustomization::FMessageTagsSettingsCustomization()
{
#if !UE_VERSION_NEWER_THAN(5, 0, 0)
	IMessageTagsModule::OnTagSettingsChanged.AddRaw(this, &FMessageTagsSettingsCustomization::OnTagTreeChanged);
#endif
}

FMessageTagsSettingsCustomization::~FMessageTagsSettingsCustomization()
{
	IMessageTagsModule::OnTagSettingsChanged.RemoveAll(this);
}

void FMessageTagsSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
#if !UE_VERSION_NEWER_THAN(5, 0, 0)
	const float MaxPropertyWidth = 480.0f;
	const float MaxPropertyHeight = 240.0f;
#endif

	IDetailCategoryBuilder& MessageTagsCategory = DetailLayout.EditCategory("MessageTags");
	{
		TArray<TSharedRef<IPropertyHandle>> MessageTagsProperties;
		MessageTagsCategory.GetDefaultProperties(MessageTagsProperties, true, true);

		TSharedPtr<IPropertyHandle> TagListProperty = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UMessageTagsList, MessageTagList), UMessageTagsList::StaticClass());
		TagListProperty->MarkHiddenByCustomization();

#if UE_VERSION_NEWER_THAN(5, 0, 0)
		TSharedPtr<IPropertyHandle> NewTagSourceProperty = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UMessageTagsSettings, NewTagSource));
		NewTagSourceProperty->MarkHiddenByCustomization();
#endif
		for (TSharedPtr<IPropertyHandle> Property : MessageTagsProperties)
		{
			if (Property->GetProperty() == TagListProperty->GetProperty())
			{
#if UE_VERSION_NEWER_THAN(5, 0, 0)
				// Button to open tag manager
				MessageTagsCategory.AddCustomRow(TagListProperty->GetPropertyDisplayName(), /*bForAdvanced*/false)
				.NameContent()
				[
					TagListProperty->CreatePropertyNameWidget()
				]
				.ValueContent()
				[
					SNew(SButton)
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Center)
					.OnClicked_Lambda([this]()
					{
						FMessageTagManagerWindowArgs Args;
						Args.bRestrictedTags = false;
						UE::MessageTags::Editor::OpenMessageTagManager(Args);
						return FReply::Handled();
					})
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(FMargin(0,0,4,0))
						[
							SNew( SImage )
							.Image(FAppStyle::GetBrush("Icons.Settings"))
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
						+SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ManageGameplayTags", "Manage Message Tags..."))
						]
					]
				];
#else
				// Button to open add source dialog
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
#endif
			}
#if UE_VERSION_NEWER_THAN(5, 0, 0)
			else if (Property->GetProperty() ==  NewTagSourceProperty->GetProperty())
			{
				// Button to open add source dialog
				MessageTagsCategory.AddCustomRow(NewTagSourceProperty->GetPropertyDisplayName(), /*bForAdvanced*/false)
				.NameContent()
				[
					NewTagSourceProperty->CreatePropertyNameWidget()
				]
				.ValueContent()
				[
					SNew(SButton)
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Center)
					.OnClicked_Lambda([this]()
					{
						const TSharedRef<SWindow> Window = SNew(SWindow)
							.Title(LOCTEXT("AddNewMessageTagSourceTitle", "Add new Message Tag Source"))
							.SizingRule(ESizingRule::Autosized)
							.SupportsMaximize(false)
							.SupportsMinimize(false)
							.Content()
							[
								SNew(SBox)
								.MinDesiredWidth(320.0f)
								[
									SNew(SAddNewMessageTagSourceWidget)
								]
							];

						GEditor->EditorAddModalWindow(Window);

						return FReply::Handled();
					})
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(FMargin(0,0,4,0))
						[
							SNew( SImage )
							.Image(FAppStyle::GetBrush("Icons.Plus"))
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
						+SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("AddNewMessageTagSource", "Add new Message Tag source..."))
						]
					]
				];
			}
#endif
			else
			{
				MessageTagsCategory.AddProperty(Property);
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
				// Button to open restricted tag manager
				AdvancedMessageTagsCategory.AddCustomRow(RestrictedTagListProperty->GetPropertyDisplayName(), true)
				.NameContent()
				[
					RestrictedTagListProperty->CreatePropertyNameWidget()
				]
				.ValueContent()
#if UE_VERSION_NEWER_THAN(5, 0, 0)
				[
					SNew(SButton)
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Center)
					.OnClicked_Lambda([this]()
					{
						FMessageTagManagerWindowArgs Args;
						Args.bRestrictedTags = true;
						UE::MessageTags::Editor::OpenMessageTagManager(Args);
						return FReply::Handled();
					})
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(FMargin(0,0,4,0))
						[
							SNew( SImage )
							.Image(FAppStyle::GetBrush("Icons.Settings"))
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
						+SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ManageRestrictedMessageTags", "Manage Restricted Message Tags..."))
						]
					]
				];
#else
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
#endif
			}
			else
			{
				AdvancedMessageTagsCategory.AddProperty(Property);
			}
		}
	}
}

#if !UE_VERSION_NEWER_THAN(5, 0, 0)
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
#endif

#undef LOCTEXT_NAMESPACE
