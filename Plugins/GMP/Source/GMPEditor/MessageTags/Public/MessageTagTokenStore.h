// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "UnrealCompatibility.h"

// NetTokenExports is still an experimental feature that depends on Iris code.
#if UE_WITH_IRIS && UE_5_05_OR_LATER

#include "Iris/ReplicationSystem/NameTokenStore.h"

struct FMessageTag;
namespace UE::Net
{

// For now, this is just a specialization of NameTokenStore
class FMessageTagTokenStore : public FNameTokenStore
{
	UE_NONCOPYABLE(FMessageTagTokenStore);
public:
	MESSAGETAGS_API explicit FMessageTagTokenStore(FNetTokenStore& TokenStore);

	// Create a NetToken for the provided name
	MESSAGETAGS_API FNetToken GetOrCreateToken(FMessageTag Tag);

	// Resolve NetToken, to resolve remote tokens RemoteTokenStoreState must be valid
	MESSAGETAGS_API FMessageTag ResolveToken(FNetToken Token, const FNetTokenStoreState* RemoteTokenStoreState = nullptr) const;

	static FName GetTokenStoreName()
	{
		return MessageTokenStoreName;
	}

private:
	inline static FName MessageTokenStoreName = TEXT("MessageTagTokenStore");
};

}
#endif // UE_WITH_IRIS