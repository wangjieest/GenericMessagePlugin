// Copyright Epic Games, Inc. All Rights Reserved.

#include "MessageTagContainerCustomization.h"

#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_NEWER_THAN(5, 0, 0)

#include "DetailWidgetRow.h"
#include "MessageTagsEditorModule.h"
#include "MessageTagsManager.h"
#include "Widgets/Input/SHyperlink.h"
#include "SMessageTagContainerCombo.h"
#include "HAL/PlatformApplicationMisc.h"
#include "ScopedTransaction.h"
#include "MessageTagEditorUtilities.h"
#include "SMessageTagPicker.h"

#define LOCTEXT_NAMESPACE "MessageTagContainerCustomization"


TSharedRef<IPropertyTypeCustomization> FMessageTagContainerCustomizationPublic::MakeInstance()
{
	return MakeShareable(new FMessageTagContainerCustomization());
}

#if UE_VERSION_OLDER_THAN(5, 2, 0)
// Deprecated version.
TSharedRef<IPropertyTypeCustomization> FMessageTagContainerCustomizationPublic::MakeInstanceWithOptions(const FMessageTagContainerCustomizationOptions& Options)
{
	return MakeShareable(new FMessageTagContainerCustomization());
}
#endif

FMessageTagContainerCustomization::FMessageTagContainerCustomization()
{
}

void FMessageTagContainerCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructPropertyHandle = InStructPropertyHandle;

	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.Padding(FMargin(0,2,0,1))
			[
				SNew(SMessageTagContainerCombo)
				.PropertyHandle(StructPropertyHandle)
			]
		]
	.PasteAction(FUIAction(
	FExecuteAction::CreateSP(this, &FMessageTagContainerCustomization::OnPasteTag),
		FCanExecuteAction::CreateSP(this, &FMessageTagContainerCustomization::CanPasteTag)));
}

void FMessageTagContainerCustomization::OnPasteTag() const
{
	if (!StructPropertyHandle.IsValid())
	{
		return;
	}
	
	FString PastedText;
	FPlatformApplicationMisc::ClipboardPaste(PastedText);
	bool bHandled = false;

	// Try to paste single tag
	const FMessageTag PastedTag = UE::MessageTags::EditorUtilities::MessageTagTryImportText(PastedText);
	if (PastedTag.IsValid())
	{
		TArray<FString> NewValues;
		SMessageTagPicker::EnumerateEditableTagContainersFromPropertyHandle(StructPropertyHandle.ToSharedRef(), [&NewValues, PastedTag](const FMessageTagContainer& EditableTagContainer)
		{
			FMessageTagContainer TagContainerCopy = EditableTagContainer;
			TagContainerCopy.AddTag(PastedTag);

			NewValues.Add(TagContainerCopy.ToString());
			return true;
		});

		FScopedTransaction Transaction(LOCTEXT("MessageTagContainerCustomization_PasteTag", "Paste Message Tag"));
		StructPropertyHandle->SetPerObjectValues(NewValues);
		bHandled = true;
	}

	// Try to paste a container
	if (!bHandled)
	{
		const FMessageTagContainer PastedTagContainer = UE::MessageTags::EditorUtilities::MessageTagContainerTryImportText(PastedText);
		if (PastedTagContainer.IsValid())
		{
			// From property
			FScopedTransaction Transaction(LOCTEXT("MessageTagContainerCustomization_PasteTagContainer", "Paste Message Tag Container"));
			StructPropertyHandle->SetValueFromFormattedString(PastedText);
			bHandled = true;
		}
	}
}

bool FMessageTagContainerCustomization::CanPasteTag() const
{
	if (!StructPropertyHandle.IsValid())
	{
		return false;
	}

	FString PastedText;
	FPlatformApplicationMisc::ClipboardPaste(PastedText);
	
	const FMessageTag PastedTag = UE::MessageTags::EditorUtilities::MessageTagTryImportText(PastedText);
	if (PastedTag.IsValid())
	{
		return true;
	}

	const FMessageTagContainer PastedTagContainer = UE::MessageTags::EditorUtilities::MessageTagContainerTryImportText(PastedText);
	if (PastedTagContainer.IsValid())
	{
		return true;
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
#endif
