//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "GMPClass2Prop.h"
#include "Templates/UnrealTemplate.h"

#include "GMPValueOneOf.generated.h"

#ifndef WITH_GMPVALUE_ONEOF
#define WITH_GMPVALUE_ONEOF 1
#endif

USTRUCT(BlueprintType, BlueprintInternalUseOnly)
struct GMP_API FGMPValueOneOf
{
	GENERATED_BODY()
public:
	bool IsValid() const { return Value.IsValid(); }

public:
	template<typename T>
	bool AsValue(T& Out, FName SubKey = {}) const
	{
#if WITH_GMPVALUE_ONEOF
		return AsValueImpl(GMP::TClass2Prop<T>::GetProperty(), &Out, SubKey);
#else
		return false;
#endif
	}

	template<typename T>
	bool AsStruct(T& Out, FName SubKey = {}, UScriptStruct* StructType = GMP::TypeTraits::StaticStruct<T>()) const
	{
#if WITH_GMPVALUE_ONEOF
		check(StructType->IsChildOf(GMP::TypeTraits::StaticStruct<T>()));
		return AsStructImpl(StructType, &Out, SubKey);
#else
		return false;
#endif
	}

	void Clear()
	{
		Value.Reset();
		Flags = 0;
	}

	int32 IterateKeyValue(int32 Idx, FString& OutKey, FGMPValueOneOf& OutValue) const { return IterateKeyValueImpl(Idx, OutKey, OutValue); }

protected:
	bool AsValueImpl(FProperty* Prop, void* Out, FName SubKey, bool bBinary = false) const;
	bool AsStructImpl(UScriptStruct* Struct, void* Out, FName SubKey, bool bBinary = false) const { return AsValueImpl(GMP::Class2Prop::TTraitsStructBase::GetProperty(Struct), Out, SubKey, bBinary); }
	// zero if err or next index otherwise INDEX_NONE
	int32 IterateKeyValueImpl(int32 Idx, FString& OutKey, FGMPValueOneOf& OutValue, bool bBinary = false) const;

	TSharedPtr<void, ESPMode::ThreadSafe> Value;
	int32 Flags = 0;
};
