//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "GMPValueOneOf.h"

#include "GMPJsonUtils.generated.h"

UENUM(BlueprintType, BlueprintInternalUseonly, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EEJsonEncodeMode : uint8
{
	Default = 0 UMETA(Hidden),
	BoolAsBoolean = 1 << 0,
	EnumAsStr = 1 << 1,
	Int64AsStr = 1 << 2,
	UInt64AsStr = 1 << 3,
	OverflowAsStr = 1 << 4,

	LowerStartCase = 1 << 6,
	StandardizeID = 1 << 7,
	All = BoolAsBoolean | EnumAsStr | Int64AsStr | UInt64AsStr | OverflowAsStr | LowerStartCase | StandardizeID,
};
ENUM_CLASS_FLAGS(EEJsonEncodeMode);

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

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "GMP|Json", meta = (CallableWithoutWorldContext, CustomStructureParam = "InData"))
	static bool EncodeJsonStr(const int32& InData, FString& OutJsonStr, UPARAM(meta = (Bitmask, BitmaskEnum = EEJsonEncodeMode)) int32 EncodeMode);
	DECLARE_FUNCTION(execEncodeJsonStr);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "GMP|Json", meta = (CallableWithoutWorldContext, CustomStructureParam = "OutData"))
	static bool DecodeJsonStr(const FString& InJsonStr, UPARAM(ref) int32& OutData);
	DECLARE_FUNCTION(execDecodeJsonStr);
};
