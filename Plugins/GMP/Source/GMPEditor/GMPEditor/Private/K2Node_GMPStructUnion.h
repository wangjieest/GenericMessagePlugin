//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EdGraph/EdGraphNodeUtils.h"
#include "Engine/MemberReference.h"
#include "K2Node.h"
#include "UObject/ObjectMacros.h"
#include "UnrealCompatibility.h"

#include "K2Node_GMPStructUnion.generated.h"

UCLASS(Abstract)
class GMPEDITOR_API UK2Node_GMPStructUnionBase : public UK2Node
{
	GENERATED_BODY()

protected:
	virtual void AllocateDefaultPins() override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual bool IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const override;
	virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual void EarlyValidation(class FCompilerResultsLog& MessageLog) const override;

	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;

	UPROPERTY()
	FName Category;

	UPROPERTY()
	bool bStructRef = true;
	UPROPERTY()
	bool bTuple = false;
	UPROPERTY()
	bool bSetVal = true;

	UScriptStruct* GetStructType() const;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	FNodeTextCache CachedNodeTitle;
};

UCLASS()
class GMPEDITOR_API UK2Node_SetStructUnion : public UK2Node_GMPStructUnionBase
{
	GENERATED_BODY()
public:
	UK2Node_SetStructUnion()
	{
		bStructRef = false;
		bTuple = false;
		bSetVal = true;
	}
};

UCLASS()
class GMPEDITOR_API UK2Node_GetStructUnion : public UK2Node_GMPStructUnionBase
{
	GENERATED_BODY()
public:
	UK2Node_GetStructUnion()
	{
		bStructRef = false;
		bTuple = false;
		bSetVal = false;
	}
};

UCLASS()
class GMPEDITOR_API UK2Node_SetStructTuple : public UK2Node_SetStructUnion
{
	GENERATED_BODY()
public:
	UK2Node_SetStructTuple()
	{
		bStructRef = true;
		bTuple = true;
	}
};

UCLASS()
class GMPEDITOR_API UK2Node_GetStructTuple : public UK2Node_GetStructUnion
{
	GENERATED_BODY()
public:
	UK2Node_GetStructTuple()
	{
		bStructRef = true;
		bTuple = true;
	}
};

UCLASS()
class GMPEDITOR_API UK2Node_SetDynStructOnScope : public UK2Node_GMPStructUnionBase
{
	GENERATED_BODY()
public:
	UK2Node_SetDynStructOnScope()
	{
		bStructRef = true;
		bSetVal = true;
	}
};

UCLASS()
class GMPEDITOR_API UK2Node_GetDynStructOnScope : public UK2Node_GMPStructUnionBase
{
	GENERATED_BODY()
public:
	UK2Node_GetDynStructOnScope()
	{
		bStructRef = true;
		bSetVal = false;
	}
};
//////////////////////////////////////////////////////////////////////////

UENUM()
enum class EGMPUnionOpType : uint8
{
	None,
	StructSetter,
	StructGetter,
	StructCleaner,
};

// meta=(GMPUnionMember ="MemberName")
UCLASS()
class UK2Node_GMPUnionMemberOp : public UK2Node
{
	GENERATED_BODY()
public:
protected:
	virtual void AllocateDefaultPins() override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;

	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual void EarlyValidation(class FCompilerResultsLog& MessageLog) const override;

	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual bool IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const override;
	virtual FBlueprintNodeSignature GetSignature() const override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;

protected:
	UPROPERTY()
	EGMPUnionOpType OpType = EGMPUnionOpType::None;

	UPROPERTY()
	FMemberReference VariableRef;

	UPROPERTY()
	FName ProxyFunctionName;

	UPROPERTY()
	TSet<FString> RestrictedClasses;

	FNodeTextCache CachedNodeTitle;
};
