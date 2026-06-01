//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EdGraph/EdGraphNodeUtils.h"
#include "Engine/MemberReference.h"
#include "GMPInvokerTypes.h"
#include "K2Node.h"
#include "UObject/ObjectMacros.h"
#include "UnrealCompatibility.h"

#include "K2Node_GMPGenericInvoker.generated.h"

// A unified reflection node: walk an optional member chain off a Target object,
// then either read the final member's value (GetMember) or call a function on the
// endpoint object (CallFunction). Editor keeps guid-backed references for rename
// auto-sync; ExpandNode flattens everything to FName literals so the compiled
// blueprint never hard-references the endpoint class (weak runtime dependency).
UCLASS()
class GMPEDITOR_API UK2Node_GMPGenericInvoker : public UK2Node
{
	GENERATED_BODY()

public:
	UK2Node_GMPGenericInvoker();

	// Member-chain prefix. Empty => Target is the endpoint object directly.
	UPROPERTY()
	TArray<FGMPMemberChainLink> MemberChain;

	// What to do at the endpoint.
	UPROPERTY()
	EGMPInvokeEndpoint EndpointMode = EGMPInvokeEndpoint::GetMember;

	// CallFunction mode: the function to invoke on the endpoint object.
	UPROPERTY()
	FMemberReference FunctionRef;

	// CallFunction mode: cached signature for pin (re)building.
	UPROPERTY()
	TArray<FGMPCallParamCache> CachedParams;

	// GetMember mode: cached leaf type for the wildcard output pin.
	UPROPERTY()
	FEdGraphPinType CachedOutputType;

	UPROPERTY()
	FName EndpointClassName;

	UPROPERTY()
	bool bResolveFailed = false;

	static const FName PN_Target;
	static const FName PN_Result;   // GetMember mode wildcard output

	//~ Begin UEdGraphNode Interface
	virtual void AllocateDefaultPins() override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void PostReconstructNode() override;
	virtual TSharedPtr<class SGraphNode> CreateVisualWidget() override;
	//~ End UEdGraphNode Interface

	//~ Begin UK2Node Interface
	virtual bool IsNodePure() const override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void EarlyValidation(class FCompilerResultsLog& MessageLog) const override;
	virtual void HandleVariableRenamed(UBlueprint* InBlueprint, UClass* InVariableClass, UEdGraph* InGraph, const FName& InOldVarName, const FName& InNewVarName) override;
	//~ End UK2Node Interface

	//~ Begin UObject Interface
	virtual void PostLoad() override;
	//~ End UObject Interface

	// --- helpers (also used by the details customization) ---

	// Class connected to the Target pin (follows knots), or null.
	UClass* GetTargetPinClass() const;

	// The owner UStruct to enumerate members for at a given chain level.
	UStruct* GetOwnerStructForLevel(int32 Level) const;

	// Endpoint class after walking the whole chain; empty chain => Target class.
	UClass* GetEndpointClass() const;

	// Resolve every chain level (rename sync). Returns props if requested.
	bool ResolveChain(TArray<FProperty*>* OutProps = nullptr);

	// Resolve the endpoint function (CallFunction mode), rename sync.
	UFunction* ResolveFunction() const;

	// Details-panel callbacks.
	void SetMemberAtLevel(int32 Level, FProperty* Picked);
	void SetEndpointMode(EGMPInvokeEndpoint Mode);
	void SetFunction(UFunction* Fn);
	void RemoveLevelsFrom(int32 Level);
	void RebuildParamCaches(UFunction* Fn);

private:
	void ClassifyAndCache(FGMPMemberChainLink& Link, FProperty* Prop) const;
	bool IsFunctionPure() const;

	FNodeTextCache CachedNodeTitle;
};
