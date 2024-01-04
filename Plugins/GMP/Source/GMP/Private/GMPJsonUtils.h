//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "GMPValueOneOf.h"

#include "GMPJsonUtils.generated.h"

UCLASS()
class UGMPJsonUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "GMP|OneOf(Json)", meta = (CallableWithoutWorldContext, CustomStructureParam = "InOut", AdvancedDisplay = "bComsume"))
	static bool AsStruct(const FGMPValueOneOf& InValue, UPARAM(ref) int32& InOut, FName SubKey, bool bComsume = false);
	DECLARE_FUNCTION(execAsStruct);

	UFUNCTION(BlueprintCallable, Category = "GMP|OneOf(Json)", meta = (CallableWithoutWorldContext))
	static void ClearOneOf(UPARAM(ref) FGMPValueOneOf& InValue);

protected:
	static bool AsValueImpl(const FGMPValueOneOf& In, FProperty* Prop, void* Out, FName SubKey);
	static int32 IterateKeyValueImpl(const FGMPValueOneOf& In, int32 Idx, FString& OutKey, FGMPValueOneOf& OutValue);

	friend struct FGMPValueOneOf;
};
