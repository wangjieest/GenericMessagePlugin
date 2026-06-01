//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "UObject/WeakObjectPtr.h"

class IDetailLayoutBuilder;
class IDetailCategoryBuilder;
class UK2Node_GMPGenericInvoker;

// Details-panel UI for UK2Node_GMPGenericInvoker:
//  - one cascading member dropdown per chain level,
//  - an endpoint-mode toggle (Get Member / Call Function),
//  - a function dropdown when in Call Function mode.
class FGMPGenericInvokerCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;

private:
	void BuildChainRow(IDetailCategoryBuilder& Category, int32 Level);
	void BuildEndpointRows(IDetailCategoryBuilder& Category);
	TSharedRef<class SWidget> MakeChainMenu(int32 Level);
	TSharedRef<class SWidget> MakeFunctionMenu();
	FText GetChainText(int32 Level) const;
	FText GetFunctionText() const;

	TWeakObjectPtr<UK2Node_GMPGenericInvoker> NodePtr;
	TSharedPtr<class IPropertyUtilities> PropertyUtilities;
};
