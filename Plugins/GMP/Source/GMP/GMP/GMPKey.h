//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "GMPKey.generated.h"

#ifndef GMP_WITH_SIGNAL_ORDER
#define GMP_WITH_SIGNAL_ORDER 1
#endif

namespace GMP
{
struct FGMPListenOrder
{
#if GMP_WITH_SIGNAL_ORDER
	int32 Order = 0;
#endif
	FGMPListenOrder(int32 InOrder = 0)
#if GMP_WITH_SIGNAL_ORDER
		: Order(InOrder)
#endif
	{
	}
	GMP_API static FGMPListenOrder MaxOrder;
	GMP_API static FGMPListenOrder MinOrder;
};

struct FGMPListenOptions : public FGMPListenOrder
{
	FGMPListenOptions() {}

	FGMPListenOptions(int32 InTimes, int32 InOrder = 0)
		: FGMPListenOrder(InOrder)
		, Times(InTimes)
	{
	}
	FGMPListenOptions(FGMPListenOrder InOrder, int32 InTimes = -1)
		: FGMPListenOrder(InOrder)
		, Times(InTimes)
	{
	}

	int32 Times = -1;

	GMP_API static FGMPListenOptions Default;
};
}  // namespace GMP
using FGMPListenOrder = GMP::FGMPListenOrder;

USTRUCT(BlueprintType)
struct FGMPKey
{
	GENERATED_BODY()
public:
	FGMPKey(int64 In = 0)
		: Key(In)
	{
	}

	UPROPERTY()
	int64 Key;

public:
	FORCEINLINE auto GetKey() const { return Key; }
	FORCEINLINE bool IsValid() const { return !!Key; }

	operator int64() const { return Key; }
	explicit operator bool() const { return IsValid(); }
	FString ToString() const { return LexToString(Key); }

	GMP_API static FGMPKey NextGMPKey();
	GMP_API static FGMPKey NextGMPKey(GMP::FGMPListenOptions Options);

	friend uint32 GetTypeHash(FGMPKey In) { return GetTypeHash(In.Key); }
	friend bool operator==(const FGMPKey& Lhs, const FGMPKey& Rhs) { return (Lhs.Key == Rhs.Key); }
	friend bool operator<(const FGMPKey& Lhs, const FGMPKey& Rhs) { return (Lhs.Key < Rhs.Key); }
};
