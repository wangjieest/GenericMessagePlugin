// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdGraphUtilities.h"
#include "MessageTagContainer.h"
#include "EdGraphSchema_K2.h"
#include "SMessageTagGraphPin.h"
#include "SMessageTagContainerGraphPin.h"
//#include "SMessageTagQueryGraphPin.h"

class FMessageTagsGraphPanelPinFactory: public FGraphPanelPinFactory
{
	virtual TSharedPtr<class SGraphPin> CreatePin(class UEdGraphPin* InPin) const override
	{
		if (InPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
		{
			if (UScriptStruct* PinStructType = Cast<UScriptStruct>(InPin->PinType.PinSubCategoryObject.Get()))
			{
				if (PinStructType->IsChildOf(FMessageTag::StaticStruct()))
				{
					return SNew(SMessageTagGraphPin, InPin);
				}
				else if (PinStructType->IsChildOf(FMessageTagContainer::StaticStruct()))
				{
					return SNew(SMessageTagContainerGraphPin, InPin);
				}
				#if 0
				else if (PinStructType->IsChildOf(FMessageTagQuery::StaticStruct()))
				{
					return SNew(SMessageTagQueryGraphPin, InPin);
				}
				#endif
			}
		}
		else if (InPin->PinType.PinCategory == UEdGraphSchema_K2::PC_String && InPin->PinType.PinSubCategory == TEXT("LiteralMessageTagContainer"))
		{
			return SNew(SMessageTagContainerGraphPin, InPin);
		}

		return nullptr;
	}
};
