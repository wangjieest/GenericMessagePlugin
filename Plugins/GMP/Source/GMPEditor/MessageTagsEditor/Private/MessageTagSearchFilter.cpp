// Copyright Epic Games, Inc. All Rights Reserved.

#include "MessageTagSearchFilter.h"
#include "Framework/Commands/UIAction.h"
#include "Engine/Blueprint.h"
#include "UObject/UnrealType.h"
#include "Misc/ConfigCacheIni.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "MessageTagContainer.h"
#include "MessageTagsManager.h"
#include "SMessageTagWidget.h"


#define LOCTEXT_NAMESPACE "MessageTagSearchFilter"

//////////////////////////////////////////////////////////////////////////
//

/** A filter that search for assets using a specific message tag */
class FFrontendFilter_MessageTags : public FFrontendFilter
{
public:
	FFrontendFilter_MessageTags(TSharedPtr<FFrontendFilterCategory> InCategory)
		: FFrontendFilter(InCategory)
	{
		TagContainer = MakeShareable(new FMessageTagContainer);

		EditableContainers.Add(SMessageTagWidget::FEditableMessageTagContainerDatum(/*TagContainerOwner=*/ nullptr, TagContainer.Get()));
	}

	// FFrontendFilter implementation
	virtual FLinearColor GetColor() const override { return FLinearColor::Red; }
	virtual FString GetName() const override { return TEXT("MessageTagFilter"); }
	virtual FText GetDisplayName() const override;
	virtual FText GetToolTipText() const override;
	virtual void ModifyContextMenu(FMenuBuilder& MenuBuilder) override;
	virtual void SaveSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) const override;
	virtual void LoadSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) override;
	// End of FFrontendFilter implementation

	// IFilter implementation
	virtual bool PassesFilter(FAssetFilterType InItem) const override;
	// End of IFilter implementation

protected:
	// Container of selected search tags (the asset is shown if *any* of these match)
	TSharedPtr<FMessageTagContainer> TagContainer;

	// Adaptor for the SMessageTagWidget to edit our tag container
	TArray<SMessageTagWidget::FEditableMessageTagContainerDatum> EditableContainers;

protected:
	bool ProcessStruct(void* Data, UStruct* Struct) const;

	bool ProcessProperty(void* Data, FProperty* Prop) const;

	void OnTagWidgetChanged();
};

void FFrontendFilter_MessageTags::ModifyContextMenu(FMenuBuilder& MenuBuilder)
{
	FUIAction Action;

	MenuBuilder.BeginSection(TEXT("ComparsionSection"), LOCTEXT("ComparisonSectionHeading", "Message Tag(s) to search for"));

	TSharedRef<SWidget> TagWidget =
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		.MaxHeight(300)
		[
			SNew(SMessageTagWidget, EditableContainers)
			.MultiSelect(true)
			.OnTagChanged_Raw(this, &FFrontendFilter_MessageTags::OnTagWidgetChanged)
		];
 	MenuBuilder.AddWidget(TagWidget, FText::GetEmpty(), /*bNoIndent=*/ false);
}

FText FFrontendFilter_MessageTags::GetDisplayName() const
{
	if (TagContainer->Num() == 0)
	{
		return LOCTEXT("AnyMessageTagDisplayName", "Message Tags");
	}
	else
	{
		FString QueryString;

		int32 Count = 0;
		for (const FMessageTag& Tag : *TagContainer.Get())
		{
			if (Count > 0)
			{
				QueryString += TEXT(" | ");
			}

			QueryString += Tag.ToString();
			++Count;
		}


		return FText::Format(LOCTEXT("MessageTagListDisplayName", "Message Tags ({0})"), FText::AsCultureInvariant(QueryString));
	}
}

FText FFrontendFilter_MessageTags::GetToolTipText() const
{
	if (TagContainer->Num() == 0)
	{
		return LOCTEXT("AnyMessageTagFilterDisplayTooltip", "Search for any *loaded* Blueprint or asset that contains a message tag (right-click to choose tags).");
	}
	else
	{
		return LOCTEXT("MessageTagFilterDisplayTooltip", "Search for any *loaded* Blueprint or asset that has a message tag which matches any of the selected tags (right-click to choose tags).");
	}
}

void FFrontendFilter_MessageTags::SaveSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) const
{
	TArray<FString> TagStrings;
	TagStrings.Reserve(TagContainer->Num());
	for (const FMessageTag& Tag : *TagContainer.Get())
	{
		TagStrings.Add(Tag.GetTagName().ToString());
	}

	GConfig->SetArray(*IniSection, *(SettingsString + TEXT(".Tags")), TagStrings, IniFilename);
}

void FFrontendFilter_MessageTags::LoadSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString)
{
	UMessageTagsManager& Manager = UMessageTagsManager::Get();

	TArray<FString> TagStrings;
	GConfig->GetArray(*IniSection, *(SettingsString + TEXT(".Tags")), /*out*/ TagStrings, IniFilename);

	TagContainer->Reset();
	for (const FString& TagString : TagStrings)
	{
		FMessageTag NewTag = Manager.RequestMessageTag(*TagString, /*bErrorIfNotFound=*/ false);
		if (NewTag.IsValid())
		{
			TagContainer->AddTag(NewTag);
		}
	}
}

void FFrontendFilter_MessageTags::OnTagWidgetChanged()
{
	BroadcastChangedEvent();
}

bool FFrontendFilter_MessageTags::ProcessStruct(void* Data, UStruct* Struct) const
{
	for (TFieldIterator<FProperty> PropIt(Struct, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
	{
		auto* Prop = *PropIt;

		if (ProcessProperty(Data, Prop))
		{
			return true;
		}
	}

	return false;
}

bool FFrontendFilter_MessageTags::ProcessProperty(void* Data, FProperty* Prop) const
{
	void* InnerData = Prop->ContainerPtrToValuePtr<void>(Data);

	if (FStructProperty* StructProperty = CastField<FStructProperty>(Prop))
	{
		if (StructProperty->Struct == FMessageTag::StaticStruct())
		{
			FMessageTag& ThisTag = *static_cast<FMessageTag*>(InnerData);

			const bool bAnyTagIsOK = TagContainer->Num() == 0;
			const bool bPassesTagSearch = bAnyTagIsOK || ThisTag.MatchesAny(*TagContainer);

			return bPassesTagSearch;
		}
		else
		{
			return ProcessStruct(InnerData, StructProperty->Struct);
		}
	}
	else if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Prop))
	{
		FScriptArrayHelper ArrayHelper(ArrayProperty, InnerData);
		for (int32 ArrayIndex = 0; ArrayIndex < ArrayHelper.Num(); ++ArrayIndex)
		{
			void* ArrayData = ArrayHelper.GetRawPtr(ArrayIndex);

			if (ProcessProperty(ArrayData, ArrayProperty->Inner))
			{
				return true;
			}
		}
	}

	return false;
}

bool FFrontendFilter_MessageTags::PassesFilter(FAssetFilterType InItem) const
{
	UObject* Object = nullptr;
#if UE_4_26_OR_LATER
	FAssetData ItemAssetData;
	if (InItem.Legacy_TryGetAssetData(ItemAssetData))
		Object = ItemAssetData.FastGetAsset(false);

#elif UE_4_23_OR_LATER
	Object = InItem.FastGetAsset(false);
#else
	Object = InItem.GetAsset();
#endif
	if (Object)
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
		{
			return ProcessStruct(Blueprint->GeneratedClass->GetDefaultObject(), Blueprint->GeneratedClass);

			//@TODO: Check blueprint bytecode!
		}
		else if (UClass* Class = Cast<UClass>(Object))
		{
			return ProcessStruct(Class->GetDefaultObject(), Class);
		}
		else
		{
			return ProcessStruct(Object, Object->GetClass());
		}
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////
//

void UMessageTagSearchFilter::AddFrontEndFilterExtensions(TSharedPtr<FFrontendFilterCategory> DefaultCategory, TArray< TSharedRef<class FFrontendFilter> >& InOutFilterList) const
{
	InOutFilterList.Add(MakeShareable(new FFrontendFilter_MessageTags(DefaultCategory)));
}

#undef LOCTEXT_NAMESPACE
