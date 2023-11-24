// Copyright Epic Games, Inc. All Rights Reserved.

#include "MessageTagEditorUtilities.h"
#include "MessageTagsManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableSet.h"
#include "K2Node_FunctionTerminator.h"
#include "Misc/OutputDeviceNull.h"

namespace UE::MessageTags::EditorUtilities
{

static FName NAME_Categories = FName("Categories");

FString ExtractTagFilterStringFromGraphPin(UEdGraphPin* InTagPin)
{
	FString FilterString;

	if (ensure(InTagPin))
	{
		const UMessageTagsManager& TagManager = UMessageTagsManager::Get();
		if (UScriptStruct* PinStructType = Cast<UScriptStruct>(InTagPin->PinType.PinSubCategoryObject.Get()))
		{
			FilterString = TagManager.GetCategoriesMetaFromField(PinStructType);
		}

		UEdGraphNode* OwningNode = InTagPin->GetOwningNode();

		if (FilterString.IsEmpty())
		{
			FilterString = OwningNode->GetPinMetaData(InTagPin->PinName, NAME_Categories);
		}

		if (FilterString.IsEmpty())
		{
			if (const UK2Node_CallFunction* CallFuncNode = Cast<UK2Node_CallFunction>(OwningNode))
			{
				if (const UFunction* TargetFunction = CallFuncNode->GetTargetFunction())
				{
					FilterString = TagManager.GetCategoriesMetaFromFunction(TargetFunction, InTagPin->PinName);
				}
			}
			else if (const UK2Node_VariableSet* VariableSetNode = Cast<UK2Node_VariableSet>(OwningNode))
			{
				if (FProperty* SetVariable = VariableSetNode->GetPropertyForVariable())
				{
					FilterString = TagManager.GetCategoriesMetaFromField(SetVariable);
				}
			}
			else if (const UK2Node_FunctionTerminator* FuncTermNode = Cast<UK2Node_FunctionTerminator>(OwningNode))
			{
				if (const UFunction* SignatureFunction = FuncTermNode->FindSignatureFunction())
				{
					FilterString = TagManager.GetCategoriesMetaFromFunction(SignatureFunction, InTagPin->PinName);
				}
			}
		}
	}

	return FilterString;
}

FString MessageTagExportText(const FMessageTag Tag)
{
	FString ExportString;
	FMessageTag::StaticStruct()->ExportText(ExportString, &Tag, &Tag, /*OwnerObject*/nullptr, /*PortFlags*/0, /*ExportRootScope*/nullptr);
	return ExportString;
}

FMessageTag MessageTagTryImportText(const FString& Text, const int32 PortFlags)
{
	FOutputDeviceNull NullOut;
	FMessageTag Tag;
	FMessageTag::StaticStruct()->ImportText(*Text, &Tag, /*OwnerObject*/nullptr, PortFlags, &NullOut, FMessageTag::StaticStruct()->GetName(), /*bAllowNativeOverride*/true);
	return Tag;
}

FString MessageTagContainerExportText(const FMessageTagContainer& TagContainer)
{
	FString ExportString;
	FMessageTagContainer::StaticStruct()->ExportText(ExportString, &TagContainer, &TagContainer, /*OwnerObject*/nullptr, /*PortFlags*/0, /*ExportRootScope*/nullptr);
	return ExportString;
}

FMessageTagContainer MessageTagContainerTryImportText(const FString& Text, const int32 PortFlags)
{
	FOutputDeviceNull NullOut;
	FMessageTagContainer TagContainer;
	FMessageTagContainer::StaticStruct()->ImportText(*Text, &TagContainer, /*OwnerObject*/nullptr, PortFlags, &NullOut, FMessageTagContainer::StaticStruct()->GetName(), /*bAllowNativeOverride*/true);
	return TagContainer;
}

};