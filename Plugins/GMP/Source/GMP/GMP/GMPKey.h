//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "GMPKey.generated.h"

USTRUCT(BlueprintType)
struct FGMPKey
{
	GENERATED_BODY()
public:
	FGMPKey(uint64 In = 0u)
		: Key(In)
	{
	}

	UPROPERTY()
	uint64 Key;

public:
	FORCEINLINE auto GetKey() const { return Key; }
	FORCEINLINE bool IsValid() const { return !!Key; }

	operator uint64() const { return Key; }
	explicit operator bool() const { return IsValid(); }
	FString ToString() const { return LexToString(Key); }

	GMP_API static FGMPKey NextGMPKey();

	friend uint32 GetTypeHash(FGMPKey In) { return GetTypeHash(In.Key); }
	friend bool operator==(const FGMPKey& Lhs, const FGMPKey& Rhs) { return (Lhs.Key == Rhs.Key); }
};
