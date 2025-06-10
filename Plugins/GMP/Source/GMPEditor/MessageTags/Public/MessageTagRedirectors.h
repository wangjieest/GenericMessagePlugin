// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPath.h"
#include "MessageTagContainer.h"

#include "MessageTagRedirectors.generated.h"

#if WITH_EDITOR && UE_5_06_OR_LATER
class FBlake3;
#endif

/** A single redirect from a deleted tag to the new tag that should replace it */
USTRUCT()
struct MESSAGETAGS_API FMessageTagRedirect
{
	GENERATED_BODY()

public:
	FMessageTagRedirect() { }

	UPROPERTY(EditAnywhere, Category = MessageTags)
	FName OldTagName;

	UPROPERTY(EditAnywhere, Category = MessageTags)
	FName NewTagName;

	friend inline bool operator==(const FMessageTagRedirect& A, const FMessageTagRedirect& B)
	{
		return A.OldTagName == B.OldTagName && A.NewTagName == B.NewTagName;
	}

	// This enables lookups by old tag name via FindByKey
	bool operator==(FName OtherOldTagName) const
	{
		return OldTagName == OtherOldTagName;
	}
};

class MESSAGETAGS_API FMessageTagRedirectors
{
public:
	static FMessageTagRedirectors& Get();

	/** Sees if the tag name should be redirected to a different tag, returns null if there is no active redirect */
	const FMessageTag* RedirectTag(const FName& InTagName) const;

	/** Adds redirects from the specified source. Faster than refreshing the entire list */
	void AddRedirectsFromSource(const struct FMessageTagSource* Source);

	/** Refreshes the redirect map after a config change */
	void RefreshTagRedirects();

#if WITH_EDITOR && UE_5_06_OR_LATER
	/** Update the hasher with a deterministic hash of the existing redirects. Used for e.g. IncrementalCook keys. */
	void Hash(FBlake3& Hasher);
#endif

private:
	FMessageTagRedirectors();

	/** Adds a list of tag redirects to the map */
	void AddRedirects(const TArray<FMessageTagRedirect>& Redirects);

	/** The map of ini-configured tag redirectors */
	TMap<FName, FMessageTag> TagRedirects;
};