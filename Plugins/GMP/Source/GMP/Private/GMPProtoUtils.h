//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "Engine/UserDefinedEnum.h"
#include "Engine/UserDefinedStruct.h"
#include "GMPValueOneOf.h"
#include "Engine/DataAsset.h"

#include "GMPProtoUtils.generated.h"

UCLASS()
class UGMPProtoUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "GMP|OneOf(Proto)", meta = (CallableWithoutWorldContext, CustomStructureParam = "InOut", AdvancedDisplay = "bComsume"))
	static bool AsStruct(const FGMPValueOneOf& InValue, UPARAM(ref) int32& InOut, FName SubKey, bool bComsume = false);
	DECLARE_FUNCTION(execAsStruct);

	UFUNCTION(BlueprintCallable, Category = "GMP|OneOf(Proto)", meta = (CallableWithoutWorldContext))
	static void ClearOneOf(UPARAM(ref) FGMPValueOneOf& InValue);

protected:
	static bool AsValueImpl(const FGMPValueOneOf& In, FProperty* Prop, void* Out, FName SubKey);
	static int32 IterateKeyValueImpl(const FGMPValueOneOf& In, int32 Idx, FString& OutKey, FGMPValueOneOf& OutValue);

	friend struct FGMPValueOneOf;
};

//////////////////////////////////////////////////////////////////////////
UCLASS(MinimalAPI, Const, NotBlueprintable, BlueprintType)
class UProtoDescrotor : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadOnly, Category = "UPB")
	TArray<uint8> Desc;

	UPROPERTY(BlueprintReadOnly, Category = "UPB")
	TArray<UProtoDescrotor*> Deps;

	bool bRegistered = false;
	void RegisterProto();

protected:
	virtual void GetPreloadDependencies(TArray<UObject*>& OutDeps) override;

};

UCLASS(MinimalAPI)
class UProtoDefinedStruct : public UUserDefinedStruct
{
	GENERATED_BODY()
public:
	UPROPERTY()
	UProtoDescrotor* ProtoDesc = nullptr;

	virtual void PostLoad() override;
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
};

UCLASS(MinimalAPI)
class UProtoDefinedEnum : public UUserDefinedEnum
{
	GENERATED_BODY()
public:
	UPROPERTY()
	UProtoDescrotor* ProtoDesc = nullptr;
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
};
