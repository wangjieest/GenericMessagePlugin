// Copyright K2Neuron, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GameplayTask.h"
#include "K2Neuron.h"

#include "K2NeuronAction.generated.h"

class FBlueprintActionDatabaseRegistrar;

UCLASS()
class UK2NeuronAction final : public UK2Neuron
{
	GENERATED_BODY()

public:
	UK2NeuronAction();

protected:
	// UEdGraphNode interface
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual bool IsCompatibleWithGraph(UEdGraph const* TargetGraph) const override;
	virtual void ValidateNodeDuringCompilation(class FCompilerResultsLog& MessageLog) const override;

	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual bool HasExternalDependencies(TArray<class UStruct*>* OptionalOutput) const override;
	virtual void EarlyValidation(class FCompilerResultsLog& MessageLog) const override;
	virtual UObject* GetJumpTargetForDoubleClick() const override;
	virtual bool CanJumpToDefinition() const override;
	virtual void JumpToDefinition() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FName GetCornerIcon() const override;
	virtual FEdGraphNodeDeprecationResponse GetDeprecationResponse(EEdGraphNodeDeprecationType DeprecationType) const override;
	// End of UEdGraphNode interface
	UEdGraph* GetFunctionGraph(const UEdGraphNode*& OutGraphNode) const;

	FText GetNodeTitleImpl(TArray<UEdGraphPin*>* InPinsToSearch = nullptr) const;

protected:
	virtual void AllocateDefaultPinsImpl(TArray<UEdGraphPin*>* InOldPins = nullptr) override;
	bool CreateSelfSpawnActions(UClass* ObjectClass, TArray<UEdGraphPin*>* InOldPins = nullptr);
	void ConfirmOutputTypes(UEdGraphPin* InTypePin = nullptr, TArray<UEdGraphPin*>* InOldPins = nullptr);

	UPROPERTY()
	UClass* ProxyFactoryClass;

	UPROPERTY()
	FName ProxyFactoryFunctionName;

	UPROPERTY()
	UClass* ProxyClass;

	UPROPERTY()
	FName ProxyActivateFunctionName = NAME_None;

	UPROPERTY()
	TSet<FString> RestrictedClasses;

	UPROPERTY()
	FName SelfObjClassPropName;

	UPROPERTY()
	TArray<FGuid> SelfImportPinGuids;
	UPROPERTY()
	TArray<FGuid> SelfSpawnedPinGuids;

	TMap<FGuid, TArray<FGuid>> DeterminesDelegateGuids;

	FName UnlinkSelfObjEventName;

	void OnSelfSpawnedObjectClassChanged(UClass* OwnerClass, const TArray<UEdGraphPin*>* InPinsToSearch = nullptr);

	void BuildRestrictedClasses() {}

	virtual UFunction* GetAlternativeAction(UClass* InClass) const override;
	UFunction* GetFactoryFunction() const;

	UEdGraphPin* GetCancelPin(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph);
	UEdGraphPin* CancelPin = nullptr;
	void FillActionDefaultCancelFlags(UClass* InClass);
	virtual bool ShouldEventParamCheckable(const FProperty* InProp) const override;
};
