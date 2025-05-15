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
	operator bool() const { return IsValid(); }

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

	bool LoadFromFile(const FString& FilePath, bool bBinary = false);
	FGMPValueOneOf SubValueOf(FName SubKey) const
	{
		FGMPValueOneOf Ret;
		AsValue(Ret, SubKey);
		return Ret;
	}

protected:
	bool AsValueImpl(FProperty* Prop, void* Out, FName SubKey, bool bBinary = false) const;
	bool AsStructImpl(UScriptStruct* Struct, void* Out, FName SubKey, bool bBinary = false) const { return AsValueImpl(GMP::Class2Prop::TTraitsStructBase::GetProperty(Struct), Out, SubKey, bBinary); }
	// zero if err or next index otherwise INDEX_NONE
	int32 IterateKeyValueImpl(int32 Idx, FString& OutKey, FGMPValueOneOf& OutValue, bool bBinary = false) const;

	TSharedPtr<void, ESPMode::ThreadSafe> Value;
	int32 Flags = 0;
};

USTRUCT()
struct FGMPPropProxyBase
{
	GENERATED_BODY()
public:
};

//////////////////////////////////////////////////////////////////////////
USTRUCT()
struct FGMPPropProxyBool : public FGMPPropProxyBase
{
	GENERATED_BODY()
public:
	UPROPERTY()
	bool Value = false;
};

USTRUCT()
struct FGMPPropProxyFloat : public FGMPPropProxyBase
{
	GENERATED_BODY()
public:
	UPROPERTY()
	float Value = 0.f;
};

USTRUCT()
struct FGMPPropProxyDouble : public FGMPPropProxyBase
{
	GENERATED_BODY()
public:
	UPROPERTY()
	double Value = 0.0;
};

USTRUCT()
struct FGMPPropProxyInt32 : public FGMPPropProxyBase
{
	GENERATED_BODY()
public:
	UPROPERTY()
	int32 Value = 0;
};

USTRUCT()
struct FGMPPropProxyInt64 : public FGMPPropProxyBase
{
	GENERATED_BODY()
public:
	UPROPERTY()
	int64 Value = 0;
};

USTRUCT()
struct FGMPPropProxyStr : public FGMPPropProxyBase
{
	GENERATED_BODY()
public:
	UPROPERTY()
	FString Value = TEXT("");
};

USTRUCT()
struct FGMPPropProxyBytes : public FGMPPropProxyBase
{
	GENERATED_BODY()
public:
	UPROPERTY()
	TArray<uint8> Value;
};

//////////////////////////////////////////////////////////////////////////
USTRUCT()
struct FGMPPropProxyInt8 : public FGMPPropProxyBase
{
	GENERATED_BODY()
public:
	UPROPERTY()
	int8 Value = 0;
};

USTRUCT()
struct FGMPPropProxyInt16 : public FGMPPropProxyBase
{
	GENERATED_BODY()
public:
	UPROPERTY()
	int16 Value = 0;
};

USTRUCT()
struct FGMPPropProxyName : public FGMPPropProxyBase
{
	GENERATED_BODY()
public:
	UPROPERTY()
	FName Value;
};

USTRUCT()
struct FGMPPropProxyText : public FGMPPropProxyBase
{
	GENERATED_BODY()
public:
	UPROPERTY()
	FText Value;
};

USTRUCT()
struct FGMPPropProxyObject : public FGMPPropProxyBase
{
	GENERATED_BODY()
public:
	UPROPERTY()
	UObject* Value = nullptr;
};

USTRUCT()
struct FGMPPropProxySoftObject : public FGMPPropProxyBase
{
	GENERATED_BODY()
public:
	UPROPERTY()
	FSoftObjectPath Value;
};

USTRUCT()
struct FGMPPropProxyClass : public FGMPPropProxyBase
{
	GENERATED_BODY()
public:
	UPROPERTY()
	UClass* Value = nullptr;
};

USTRUCT()
struct FGMPPropProxySoftClass : public FGMPPropProxyBase
{
	GENERATED_BODY()
public:
	UPROPERTY()
	FSoftClassPath Value;
};
