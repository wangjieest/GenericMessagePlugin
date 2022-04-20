// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "EdGraphUtilities.h"
#include "MessageTagContainer.h"
#include "EdGraphSchema_K2.h"
#include "SGraphPin.h"
#include "SMessageTagGraphPin.h"

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
			}
		}

		return nullptr;
	}
};
