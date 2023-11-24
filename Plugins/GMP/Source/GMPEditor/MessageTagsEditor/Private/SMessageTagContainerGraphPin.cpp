// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMessageTagContainerGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "MessageTagContainer.h"
#include "SMessageTagContainerCombo.h"
#include "MessageTagEditorUtilities.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "MessageTagContainerGraphPin"

void SMessageTagContainerGraphPin::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
}

void SMessageTagContainerGraphPin::ParseDefaultValueData()
{
	// Read using import text, but with serialize flag set so it doesn't always throw away invalid ones
	MessageTagContainer.FromExportString(GraphPinObj->GetDefaultAsString(), PPF_SerializedAsImportText);
}

void SMessageTagContainerGraphPin::OnTagContainerChanged(const FMessageTagContainer& NewTagContainer)
{
	MessageTagContainer = NewTagContainer;

	const FString TagContainerString = UE::MessageTags::EditorUtilities::MessageTagContainerExportText(MessageTagContainer);

	FString CurrentDefaultValue = GraphPinObj->GetDefaultAsString();
	if (CurrentDefaultValue.IsEmpty())
	{
		CurrentDefaultValue = UE::MessageTags::EditorUtilities::MessageTagContainerExportText(FMessageTagContainer());
	}
			
	if (!CurrentDefaultValue.Equals(TagContainerString))
	{
		const FScopedTransaction Transaction(LOCTEXT("ChangeDefaultValue", "Change Pin Default Value"));
		GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, TagContainerString);
	}
}

TSharedRef<SWidget> SMessageTagContainerGraphPin::GetDefaultValueWidget()
{
	if (GraphPinObj == nullptr)
	{
		return SNullWidget::NullWidget;
	}

	ParseDefaultValueData();
	const FString FilterString = UE::MessageTags::EditorUtilities::ExtractTagFilterStringFromGraphPin(GraphPinObj);

	return SNew(SMessageTagContainerCombo)
		.Visibility(this, &SGraphPin::GetDefaultValueVisibility)
		.Filter(FilterString)
		.TagContainer(this, &SMessageTagContainerGraphPin::GetTagContainer)
		.OnTagContainerChanged(this, &SMessageTagContainerGraphPin::OnTagContainerChanged);
}

FMessageTagContainer SMessageTagContainerGraphPin::GetTagContainer() const
{
	return MessageTagContainer;
}

#undef LOCTEXT_NAMESPACE
