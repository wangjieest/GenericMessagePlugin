// Copyright Epic Games, Inc. All Rights Reserved.

// NetTokenExports is still an experimental feature that depends on Iris code.
#include "MessageTagTokenStore.h"
#if UE_WITH_IRIS && UE_5_05_OR_LATER

#include "MessageTagContainer.h"
#include "MessageTagsManager.h"

namespace UE::Net
{

 FMessageTagTokenStore::FMessageTagTokenStore(FNetTokenStore& TokenStore) : FNameTokenStore(TokenStore)
 {
 }

FNetToken FMessageTagTokenStore::GetOrCreateToken(FMessageTag Tag)
{ 
	return FNameTokenStore::GetOrCreateToken(Tag.GetTagName());
}

FMessageTag FMessageTagTokenStore::ResolveToken(FNetToken Token, const FNetTokenStoreState* RemoteTokenStoreState) const
{
	const FName TagName = FNameTokenStore::ResolveToken(Token, RemoteTokenStoreState);
	return TagName.IsNone() ? FMessageTag() : UMessageTagsManager::Get().RequestMessageTag(TagName);
}

}

#endif // UE_WITH_IRIS