//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "UObject/WeakFieldPtr.h"
#include "IDetailRootObjectCustomization.h"

class IDetailLayoutBuilder;
class IBlueprintEditor;
class UBlueprint;

class IGMPDetailCustomization : public IDetailCustomization
{
	IGMPDetailCustomization(TSharedPtr<IBlueprintEditor> InBlueprintEditor, UBlueprint* Blueprint)
		: BlueprintEditorPtr(InBlueprintEditor)
		, BlueprintPtr(Blueprint)
	{
	}

protected:
	template<typename T>
	static TSharedPtr<IDetailCustomization> MakeInstance(TSharedPtr<IBlueprintEditor> InBlueprintEditor);

	TWeakPtr<IBlueprintEditor> BlueprintEditorPtr;
	TWeakObjectPtr<UBlueprint> BlueprintPtr;

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override { MyCustomizeDetails(DetailLayout); }
	virtual bool MyCustomizeDetails(IDetailLayoutBuilder& DetailLayout) { return false; }
};

class FGMPBPMetaCustomization : public IGMPDetailCustomization
{
public:
	static TSharedPtr<IDetailCustomization> MakeInstance(TSharedPtr<IBlueprintEditor> InBlueprintEditor) { return IGMPDetailCustomization::MakeInstance<FGMPBPMetaCustomization>(InBlueprintEditor); }

protected:
	using IGMPDetailCustomization::IGMPDetailCustomization;
	virtual bool MyCustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
};
class FGMPBPMetaFunctionCustomization : public IGMPDetailCustomization
{
public:
	static TSharedPtr<IDetailCustomization> MakeInstance(TSharedPtr<IBlueprintEditor> InBlueprintEditor) { return IGMPDetailCustomization::MakeInstance<FGMPBPMetaFunctionCustomization>(InBlueprintEditor); }

protected:
	using IGMPDetailCustomization::IGMPDetailCustomization;
	virtual bool MyCustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
};
class FGMPBPMetaParametersCustomization : public FGMPBPMetaFunctionCustomization
{
public:
	static TSharedPtr<IDetailCustomization> MakeInstance(TSharedPtr<IBlueprintEditor> InBlueprintEditor) { return IGMPDetailCustomization::MakeInstance<FGMPBPMetaParametersCustomization>(InBlueprintEditor); }

protected:
	using FGMPBPMetaFunctionCustomization::FGMPBPMetaFunctionCustomization;
	virtual bool MyCustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
};
