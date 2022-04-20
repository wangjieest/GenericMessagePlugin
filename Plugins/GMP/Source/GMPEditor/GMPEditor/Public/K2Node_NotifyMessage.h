//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EdGraph/EdGraphNodeUtils.h"
#include "K2Node_AddPinInterface.h"
#include "K2Node_MessageBase.h"
#include "Textures/SlateIcon.h"
#include "UObject/ObjectMacros.h"

#include "K2Node_NotifyMessage.generated.h"

class FBlueprintActionDatabaseRegistrar;
class UDataTable;
class UEdGraph;

UCLASS()
class GMPEDITOR_API UK2Node_NotifyMessage : public UK2Node_MessageBase
{
	GENERATED_BODY()
public:
	UK2Node_NotifyMessage();

protected:
	UPROPERTY()
	int32 InnerVer = 0;

	UPROPERTY()
	mutable int NumAdditionalInputs = 0;

	//~ Begin UK2Node_MessageBase Interface.
	virtual UEdGraphPin* AddMessagePin(int32 Index, bool bTransaction = true) override;
	virtual UEdGraphPin* AddResponsePin(int32 Index, bool bTransaction = true) override;
	virtual bool IsParameterIgnorable() const { return false; }
	virtual FName GetMessageSignature() const;
	virtual UEdGraphPin* GetMessagePin(int32 Index, TArray<UEdGraphPin*>* InPins = nullptr, bool bEnsure = true) const;
	virtual UEdGraphPin* GetResponsePin(int32 Index, TArray<UEdGraphPin*>* InPins = nullptr, bool bEnsure = true) const override;
	virtual UEdGraphPin* CreateResponseExecPin() override;
	virtual UEdGraphPin* GetResponseExecPin() const override;
	virtual int& GetMessageCount() const { return NumAdditionalInputs; }
	//~ End UK2Node_MessageBase Interface.

	virtual UEdGraphPin* GetInputPinByIndex(int32 Index) const override { return GetMessagePin(Index); }
	virtual UEdGraphPin* GetOutputPinByIndex(int32 Index) const override { return GetResponsePin(Index); }
	UEdGraphPin* AddParamPinImpl(int32 AdditionalPinIndex, bool bModify);
	UEdGraphPin* AddResponsePinImpl(int32 AdditionalPinIndex, bool bModify);
#if GMP_NODE_DETAIL
	void RemoveInputPin(UEdGraphPin* Pin);
	bool CanRemovePin(const UEdGraphPin* Pin) const;
#endif
	virtual void AllocateDefaultPinsImpl(TArray<UEdGraphPin*>* InOldPins = nullptr) override;
	//~ Begin UEdGraphNode Interface.
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	// virtual FText GetTooltipText() const override;
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual bool IsNodePure() const override { return false; }
	//~ End UEdGraphNode Interface.

	//~ Begin UK2Node Interface
	virtual bool IsNodeSafeToIgnore() const override { return true; }
	virtual void EarlyValidation(class FCompilerResultsLog& MessageLog) const override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual class FNodeHandlingFunctor* CreateNodeHandler(class FKismetCompilerContext& CompilerContext) const override;
	virtual bool IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const override;
	virtual void FixupPinDefaultValues() override;

#if WITH_EDITOR
	virtual TSharedPtr<class SGraphNode> CreateVisualWidget() override;
#endif
#if UE_4_24_OR_LATER
	virtual bool IncludeParentNodeContextMenu() const override { return true; }
#endif
	//~ End UK2Node Interface

	/** Get the spawn transform input pin */
	UEdGraphPin* GetPinByName(const FString& Index, TArray<UEdGraphPin*>* InPins = nullptr, bool bEnsure = true) const;
	void SetPinToolTip(UEdGraphPin& MutatablePin, bool bModify = false) const;

protected:
	virtual void OnNodeExpanded(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, class UK2Node_CallFunction* MessageFunc) {}
	virtual FString GetTitleHead() const override;

private:
	/** Triggers a refresh which will update the node's widget; aimed at updating the dropdown menu for the RowName input */
	void Refresh();

	/** Tooltip text for this node. */
	FText NodeTooltip;
};
