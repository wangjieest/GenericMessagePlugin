// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UnrealCompatibility.h"
#include "AssetTypeActions_Base.h"

struct FMessageTagContainer;

/** Base asset type actions for any classes with message tagging */
class MESSAGETAGSEDITOR_API FAssetTypeActions_MessageTagAssetBase : public FAssetTypeActions_Base
{

public:

	/** Constructor */
	FAssetTypeActions_MessageTagAssetBase(FName InTagPropertyName);

	/** Overridden to specify that the message tag base has actions */
	virtual bool HasActions(const TArray<UObject*>& InObjects) const override;

	/** Overridden to offer the message tagging options */
#if UE_4_24_OR_LATER
	virtual void GetActions(const TArray<UObject*>& InObjects, struct FToolMenuSection& Section) override;
#else
	virtual void GetActions(const TArray<UObject*>& InObjects, class FMenuBuilder& MenuBuilder) override;
#endif
	/** Overridden to specify misc category */
	virtual uint32 GetCategories() override;

private:
	/**
	 * Open the message tag editor
	 * 
	 * @param TagAssets	Assets to open the editor with
	 */
	void OpenMessageTagEditor(TArray<class UObject*> Objects, TArray<FMessageTagContainer> Containers) const;

	/** Name of the property of the owned message tag container */
	FName OwnedMessageTagPropertyName;
};
