// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdGraphUtilities.h"
#include "MessageTagContainer.h"
#include "EdGraphSchema_K2.h"
#include "SMessageTagGraphPin.h"
#if UE_5_00_OR_LATER
#include "SMessageTagContainerGraphPin.h"
//#include "SMessageTagQueryGraphPin.h"
#endif
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
#if UE_5_00_OR_LATER
					return SNew(SMessageTagContainerGraphPin, InPin);
#endif
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
#if UE_5_00_OR_LATER
			return SNew(SMessageTagContainerGraphPin, InPin);
#endif
		}

		return nullptr;
	}
};
