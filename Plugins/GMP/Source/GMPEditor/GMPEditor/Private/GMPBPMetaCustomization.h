//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "UObject/WeakFieldPtr.h"

class IDetailLayoutBuilder;
class IBlueprintEditor;
class UBlueprint;

class FGMPBPMetaCustomization : public IDetailCustomization
{
public:
	static TSharedPtr<IDetailCustomization> MakeInstance(TSharedPtr<IBlueprintEditor> InBlueprintEditor);

protected:
	FGMPBPMetaCustomization(TSharedPtr<IBlueprintEditor> InBlueprintEditor, UBlueprint* Blueprint)
		: BlueprintEditorPtr(InBlueprintEditor)
		, BlueprintPtr(Blueprint)
	{
	}
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;

	TWeakPtr<IBlueprintEditor> BlueprintEditorPtr;
	TWeakObjectPtr<UBlueprint> BlueprintPtr;
};
