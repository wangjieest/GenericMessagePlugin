// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_NEWER_THAN(5, 0, 0)

#include "KismetPins/SGraphPinStructInstance.h"
#include "MessageTagContainer.h"

/** Almost the same as a tag pin, but supports multiple tags */
class SMessageTagContainerGraphPin : public SGraphPinStructInstance
{
public:
	SLATE_BEGIN_ARGS(SMessageTagContainerGraphPin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);

protected:
	//~ Begin SMessageTagGraphPin Interface
	virtual void ParseDefaultValueData() override;
	virtual TSharedRef<SWidget> GetDefaultValueWidget() override;
	//~ End SGraphPin Interface

	FMessageTagContainer GetTagContainer() const;
	void OnTagContainerChanged(const FMessageTagContainer& NewTagContainer);
	
	FMessageTagContainer MessageTagContainer;
};
#endif
