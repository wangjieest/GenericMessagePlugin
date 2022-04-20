// Copyright Epic Games, Inc. All Rights Reserved.

#include "MessageTagCustomization.h"
#include "Widgets/Input/SComboButton.h"

#include "Editor.h"
#include "PropertyHandle.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "MessageTagsEditorModule.h"
#include "Widgets/Input/SHyperlink.h"
#include "UnrealCompatibility.h"

#define LOCTEXT_NAMESPACE "MessageTagCustomization"

TSharedRef<IPropertyTypeCustomization> FMessageTagCustomizationPublic::MakeInstance()
{
	return MakeShareable(new FMessageTagCustomization);
}

void FMessageTagCustomization::CustomizeHeader(TSharedRef<class IPropertyHandle> InStructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	TagContainer = MakeShareable(new FMessageTagContainer);
	StructPropertyHandle = InStructPropertyHandle;

	FSimpleDelegate OnTagChanged = FSimpleDelegate::CreateSP(this, &FMessageTagCustomization::OnPropertyValueChanged);
	StructPropertyHandle->SetOnPropertyValueChanged(OnTagChanged);

	BuildEditableContainerList();

	FUIAction SearchForReferencesAction(FExecuteAction::CreateSP(this, &FMessageTagCustomization::OnSearchForReferences));

	HeaderRow
	.NameContent()
	[
		StructPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MaxDesiredWidth(512)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SAssignNew(EditButton, SComboButton)
			.OnGetMenuContent(this, &FMessageTagCustomization::GetListContent)
			.OnMenuOpenChanged(this, &FMessageTagCustomization::OnMessageTagListMenuOpenStateChanged)
			.ContentPadding(FMargin(2.0f, 2.0f))
			.MenuPlacement(MenuPlacement_BelowAnchor)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("MessageTagCustomization_Edit", "Edit"))
			]
		]
		
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SBorder)
			.Visibility(this, &FMessageTagCustomization::GetVisibilityForTagTextBlockWidget, true)
			.Padding(4.0f)
			[
				SNew(STextBlock)
				.Text(this, &FMessageTagCustomization::SelectedTag)
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SBorder)
			.Visibility(this, &FMessageTagCustomization::GetVisibilityForTagTextBlockWidget, false)
			.Padding(4.0f)
			[
				SNew(SHyperlink)
				.Text(this, &FMessageTagCustomization::SelectedTag)
				.OnNavigate( this, &FMessageTagCustomization::OnTagDoubleClicked)
			]
		]
	]
#if UE_4_20_OR_LATER
	.AddCustomContextMenuAction(SearchForReferencesAction,
		LOCTEXT("FMessageTagCustomization_SearchForReferences", "Search For References"),
		LOCTEXT("FMessageTagCustomization_SearchForReferencesTooltip", "Find references for this tag"),
		FSlateIcon())
#endif
		;

	GEditor->RegisterForUndo(this);
}

void FMessageTagCustomization::OnTagDoubleClicked()
{
	UMessageTagsManager::Get().NotifyMessageTagDoubleClickedEditor(TagName);
}

void FMessageTagCustomization::OnSearchForReferences()
{
	FName TagFName(*TagName, FNAME_Find);
#if UE_4_23_OR_LATER
	if (FEditorDelegates::OnOpenReferenceViewer.IsBound() && !TagFName.IsNone())
#else
	if (!TagFName.IsNone())
#endif
	{
		TArray<FAssetIdentifier> AssetIdentifiers;
		AssetIdentifiers.Emplace(FMessageTag::StaticStruct(), TagFName);
		extern void MesageTagsEditor_SearchMessageReferences(const TArray<FAssetIdentifier>& AssetIdentifiers);
		MesageTagsEditor_SearchMessageReferences(AssetIdentifiers);
	}
}

EVisibility FMessageTagCustomization::GetVisibilityForTagTextBlockWidget(bool ForTextWidget) const
{
	return (UMessageTagsManager::Get().ShowMessageTagAsHyperLinkEditor(TagName) ^ ForTextWidget) ? EVisibility::Visible : EVisibility::Collapsed;
}

TSharedRef<SWidget> FMessageTagCustomization::GetListContent()
{
	BuildEditableContainerList();
	
	FString Categories = UMessageTagsManager::Get().GetCategoriesMetaFromPropertyHandle(StructPropertyHandle);

	bool bReadOnly = StructPropertyHandle->IsEditConst();

	TSharedRef<SMessageTagWidget> TagWidget =
		SNew(SMessageTagWidget, EditableContainers)
		.Filter(Categories)
		.ReadOnly(bReadOnly)
		.TagContainerName(StructPropertyHandle->GetPropertyDisplayName().ToString())
		.MultiSelect(false)
		.OnTagChanged(this, &FMessageTagCustomization::OnTagChanged)
		.PropertyHandle(StructPropertyHandle);

	LastTagWidget = TagWidget;

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.MaxHeight(400)
		[
			TagWidget
		];
}

void FMessageTagCustomization::OnMessageTagListMenuOpenStateChanged(bool bIsOpened)
{
	if (bIsOpened)
	{
		TSharedPtr<SMessageTagWidget> TagWidget = LastTagWidget.Pin();
		if (TagWidget.IsValid())
		{
			EditButton->SetMenuContentWidgetToFocus(TagWidget->GetWidgetToFocusOnOpen());
		}
	}
}

void FMessageTagCustomization::OnPropertyValueChanged()
{
	TagName = TEXT("");
	if (StructPropertyHandle.IsValid() && StructPropertyHandle->GetProperty() && EditableContainers.Num() > 0)
	{
		TArray<void*> RawStructData;
		StructPropertyHandle->AccessRawData(RawStructData);
		if (RawStructData.Num() > 0)
		{
			FMessageTag* Tag = (FMessageTag*)(RawStructData[0]);
			FMessageTagContainer* Container = EditableContainers[0].TagContainer;			
			if (Tag && Container)
			{
				Container->Reset();
				Container->AddTag(*Tag);
				TagName = Tag->ToString();
			}			
		}
	}
}

void FMessageTagCustomization::OnTagChanged()
{
	TagName = TEXT("");
	if (StructPropertyHandle.IsValid() && StructPropertyHandle->GetProperty() && EditableContainers.Num() > 0)
	{
		TArray<void*> RawStructData;
		StructPropertyHandle->AccessRawData(RawStructData);
		if (RawStructData.Num() > 0)
		{
			FMessageTag* Tag = (FMessageTag*)(RawStructData[0]);			

			// Update Tag from the one selected from list
			FMessageTagContainer* Container = EditableContainers[0].TagContainer;
			if (Tag && Container)
			{
				for (auto It = Container->CreateConstIterator(); It; ++It)
				{
					*Tag = *It;
					TagName = It->ToString();
				}
			}
		}
	}
}

void FMessageTagCustomization::PostUndo(bool bSuccess)
{
	if (bSuccess && !StructPropertyHandle.IsValid())
	{
		OnTagChanged();
	}
}

void FMessageTagCustomization::PostRedo(bool bSuccess)
{
	if (bSuccess && !StructPropertyHandle.IsValid())
	{
		OnTagChanged();
	}
}

FMessageTagCustomization::~FMessageTagCustomization()
{
	GEditor->UnregisterForUndo(this);
}

void FMessageTagCustomization::BuildEditableContainerList()
{
	EditableContainers.Empty();

	if(StructPropertyHandle.IsValid() && StructPropertyHandle->GetProperty())
	{
		TArray<void*> RawStructData;
		StructPropertyHandle->AccessRawData(RawStructData);

		if (RawStructData.Num() > 0)
		{
			FMessageTag* Tag = (FMessageTag*)(RawStructData[0]);
			if (Tag->IsValid())
			{
				TagName = Tag->ToString();
				TagContainer->AddTag(*Tag);
			}
		}

		EditableContainers.Add(SMessageTagWidget::FEditableMessageTagContainerDatum(nullptr, TagContainer.Get()));
	}
}

FText FMessageTagCustomization::SelectedTag() const
{
	return FText::FromString(*TagName);
}

TSharedRef<IPropertyTypeCustomization> FMessageTagCreationWidgetHelperDetails::MakeInstance()
{
	return MakeShareable(new FMessageTagCreationWidgetHelperDetails());
}

void FMessageTagCreationWidgetHelperDetails::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{

}

void FMessageTagCreationWidgetHelperDetails::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	FString FilterString = UMessageTagsManager::Get().GetCategoriesMetaFromPropertyHandle(StructPropertyHandle);
	const float MaxPropertyWidth = 480.0f;
	const float MaxPropertyHeight = 240.0f;

	StructBuilder.AddCustomRow(NSLOCTEXT("MessageTagReferenceHelperDetails", "NewTag", "NewTag"))
		.ValueContent()
		.MaxDesiredWidth(MaxPropertyWidth)
		[
			SAssignNew(TagWidget, SMessageTagWidget, TArray<SMessageTagWidget::FEditableMessageTagContainerDatum>())
			.Filter(FilterString)
			.NewTagName(FilterString)
			.MultiSelect(false)
			.MessageTagUIMode(EMessageTagUIMode::ManagementMode)
			.MaxHeight(MaxPropertyHeight)
			.NewTagControlsInitiallyExpanded(true)
			//.OnTagChanged(this, &FMessageTagsSettingsCustomization::OnTagChanged)
		];

}

#undef LOCTEXT_NAMESPACE
