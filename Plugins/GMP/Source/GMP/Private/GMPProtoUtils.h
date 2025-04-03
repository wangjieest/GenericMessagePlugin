//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "Engine/DataAsset.h"

#include "GMPValueOneOf.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/UserDefinedEnum.h"
#if UE_5_05_OR_LATER
#include "StructUtils/UserDefinedStruct.h"
#else
#include "Engine/UserDefinedStruct.h"
#endif

#include "GMPProtoUtils.generated.h"

UCLASS()
class UGMPProtoUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "GMP|OneOf(Proto)", meta = (CallableWithoutWorldContext, CustomStructureParam = "InOut", AdvancedDisplay = "bComsume"))
	static bool AsStruct(const FGMPValueOneOf& InValue, UPARAM(ref) int32& InOut, FName SubKey, bool bConsume = false);
	DECLARE_FUNCTION(execAsStruct);

	UFUNCTION(BlueprintCallable, Category = "GMP|OneOf(Proto)", meta = (CallableWithoutWorldContext))
	static void ClearOneOf(UPARAM(ref) FGMPValueOneOf& InValue);

protected:
	static bool AsValueImpl(const FGMPValueOneOf& In, FProperty* Prop, void* Out, FName SubKey);
	static int32 IterateKeyValueImpl(const FGMPValueOneOf& In, int32 Idx, FString& OutKey, FGMPValueOneOf& OutValue);

	friend struct FGMPValueOneOf;

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "GMP|Proto", meta = (CallableWithoutWorldContext, CustomStructureParam = "InStruct"))
	static bool EncodeProto(const int32& InStruct, TArray<uint8>& InOut);
	DECLARE_FUNCTION(execEncodeProto);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "GMP|Proto", meta = (CallableWithoutWorldContext, CustomStructureParam = "InOutStruct"))
	static bool DecodeProto(const TArray<uint8>& InBuffer, UPARAM(ref) int32& InOutStruct);
	DECLARE_FUNCTION(execDecodeProto);
};

//////////////////////////////////////////////////////////////////////////
UCLASS(MinimalAPI, Const, NotBlueprintable, BlueprintType)
class UProtoDescriptor : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadOnly, Category = "UPB")
	TArray<uint8> Desc;

	UPROPERTY(BlueprintReadOnly, Category = "UPB")
	TArray<UProtoDescriptor*> Deps;

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
	FString FullName;

	UPROPERTY()
	TSoftObjectPtr<UProtoDescriptor> ProtoDesc;

	virtual void PostLoad() override;

protected:
	virtual void GetPreloadDependencies(TArray<UObject*>& OutDeps) override;
};

UCLASS(MinimalAPI)
class UProtoDefinedEnum : public UUserDefinedEnum
{
	GENERATED_BODY()
public:
	UPROPERTY()
	FString FullName;

	UPROPERTY()
	TSoftObjectPtr<UProtoDescriptor> ProtoDesc;

	virtual void PostLoad() override;

protected:
	virtual void GetPreloadDependencies(TArray<UObject*>& OutDeps) override;
};
