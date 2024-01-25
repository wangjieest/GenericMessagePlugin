// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/Class.h"
#include "UnrealCompatibility.h"
#if UE_5_00_OR_LATER
#include "Misc/ComparisonUtility.h"
#endif
#include "MessageTagContainer.generated.h"

class UEditableMessageTagQuery;
struct FMessageTagContainer;
class FJsonObject;
struct FPropertyTag;

MESSAGETAGS_API DECLARE_LOG_CATEGORY_EXTERN(LogMessageTags, Log, All);

DECLARE_STATS_GROUP_VERBOSE(TEXT("Message Tags"), STATGROUP_MessageTags, STATCAT_Advanced);

DECLARE_CYCLE_STAT_EXTERN(TEXT("FMessageTagContainer::DoesTagContainerMatch"), STAT_FMessageTagContainer_DoesTagContainerMatch, STATGROUP_MessageTags, MESSAGETAGS_API);

struct FMessageTagContainer;

// DEPRECATED ENUMS
namespace UE_DEPRECATED(5.0, "Deprecated in favor of HasExact and related functions") EMessageTagMatchType
{
	enum Type
	{
		Explicit,			// This will check for a match against just this tag
		IncludeParentTags,	// This will also check for matches against all parent tags
	};
}

UENUM(BlueprintType)
enum class EMessageContainerMatchType : uint8
{
	//	Means the filter is populated by any tag matches in this container.
	Any,	

	//	Means the filter is only populated if all of the tags in this container match.
	All		
};

typedef uint16 FMessageTagNetIndex;
#define INVALID_TAGNETINDEX MAX_uint16

/**
 * A single message tag, which represents a hierarchical name of the form x.y that is registered in the MessageTagsManager
 * You can filter the message tags displayed in the editor using, meta = (Categories = "Tag1.Tag2.Tag3"))
 */
USTRUCT(BlueprintType)
struct MESSAGETAGS_API FMessageTag
{
	GENERATED_USTRUCT_BODY()

	/** Constructors */
	FMessageTag()
	{
	}

	/**
	 * Gets the FMessageTag that corresponds to the TagName
	 *
	 * @param TagName The Name of the tag to search for
	 * @param ErrorIfNotfound: ensure() that tag exists.
	 * @return Will return the corresponding FMessageTag or an empty one if not found.
	 */
	static FMessageTag RequestMessageTag(const FName& TagName, bool ErrorIfNotFound=true);

	/** 
	 * Returns true if this is a valid message tag string (foo.bar.baz). If false, it will fill 
	 * @param TagString String to check for validity
	 * @param OutError If non-null and string invalid, will fill in with an error message
	 * @param OutFixedString If non-null and string invalid, will attempt to fix. Will be empty if no fix is possible
	 * @return True if this can be added to the tag dictionary, false if there's a syntax error
	 */
	static bool IsValidMessageTagString(const FString& TagString, FText* OutError = nullptr, FString* OutFixedString = nullptr);

	/** Operators */
	FORCEINLINE bool operator==(FMessageTag const& Other) const
	{
		return TagName == Other.TagName;
	}

	FORCEINLINE bool operator!=(FMessageTag const& Other) const
	{
		return TagName != Other.TagName;
	}

	FORCEINLINE bool operator<(FMessageTag const& Other) const
	{
#if UE_5_00_OR_LATER
		return UE::ComparisonUtility::CompareWithNumericSuffix(TagName, Other.TagName) < 0;
#elif UE_4_23_OR_LATER
		return TagName.LexicalLess(Other.TagName);
#else
		return TagName < Other.TagName;
#endif
	}

	/**
	 * Determine if this tag matches TagToCheck, expanding our parent tags
	 * "A.1".MatchesTag("A") will return True, "A".MatchesTag("A.1") will return False
	 * If TagToCheck is not Valid it will always return False
	 * 
	 * @return True if this tag matches TagToCheck
	 */
	bool MatchesTag(const FMessageTag& TagToCheck) const;

	/**
	 * Determine if TagToCheck is valid and exactly matches this tag
	 * "A.1".MatchesTagExact("A") will return False
	 * If TagToCheck is not Valid it will always return False
	 * 
	 * @return True if TagToCheck is Valid and is exactly this tag
	 */
	FORCEINLINE bool MatchesTagExact(const FMessageTag& TagToCheck) const
	{
		if (!TagToCheck.IsValid())
		{
			return false;
		}
		// Only check check explicit tag list
		return TagName == TagToCheck.TagName;
	}

	/**
	 * Check to see how closely two FMessageTags match. Higher values indicate more matching terms in the tags.
	 *
	 * @param TagToCheck	Tag to match against
	 *
	 * @return The depth of the match, higher means they are closer to an exact match
	 */
	int32 MatchesTagDepth(const FMessageTag& TagToCheck) const;

	/**
	 * Checks if this tag matches ANY of the tags in the specified container, also checks against our parent tags
	 * "A.1".MatchesAny({"A","B"}) will return True, "A".MatchesAny({"A.1","B"}) will return False
	 * If ContainerToCheck is empty/invalid it will always return False
	 *
	 * @return True if this tag matches ANY of the tags of in ContainerToCheck
	 */
	bool MatchesAny(const FMessageTagContainer& ContainerToCheck) const;

	/**
	 * Checks if this tag matches ANY of the tags in the specified container, only allowing exact matches
	 * "A.1".MatchesAny({"A","B"}) will return False
	 * If ContainerToCheck is empty/invalid it will always return False
	 *
	 * @return True if this tag matches ANY of the tags of in ContainerToCheck exactly
	 */
	bool MatchesAnyExact(const FMessageTagContainer& ContainerToCheck) const;

	/** Returns whether the tag is valid or not; Invalid tags are set to NAME_None and do not exist in the game-specific global dictionary */
	FORCEINLINE bool IsValid() const
	{
		return (TagName != NAME_None);
	}

	/** Returns reference to a MessageTagContainer containing only this tag */
	const FMessageTagContainer& GetSingleTagContainer() const;

	/** Returns direct parent MessageTag of this MessageTag, calling on x.y will return x */
	FMessageTag RequestDirectParent() const;

	/** Returns a new container explicitly containing the tags of this tag */
	FMessageTagContainer GetMessageTagParents() const;

	/** Used so we can have a TMap of this struct */
	FORCEINLINE friend uint32 GetTypeHash(const FMessageTag& Tag)
	{
		return GetTypeHash(Tag.TagName);
	}

	/** Displays message tag as a string for blueprint graph usage */
	FORCEINLINE FString ToString() const
	{
		return TagName.ToString();
	}

	/** Get the tag represented as a name */
	FORCEINLINE FName GetTagName() const
	{
		return TagName;
	}
#if UE_4_24_OR_LATER
	friend void operator<<(FStructuredArchive::FSlot Slot, FMessageTag& MessageTag)
	{
		Slot << MessageTag.TagName;
	}
#else
	friend FArchive& operator<<(FArchive& Ar, FMessageTag& MessageTag)
	{
		Ar << MessageTag.TagName;
		return Ar;
	}
#endif

	/** Overridden for fast serialize */
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);

	/** Handles fixup and errors. This is only called when not serializing a full FMessageTagContainer */
	void PostSerialize(const FArchive& Ar);
	bool NetSerialize_Packed(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);

	/** Used to upgrade a Name property to a MessageTag struct property */
#if UE_4_22_OR_LATER
	bool SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot);
#else
	bool SerializeFromMismatchedTag(const FPropertyTag& Tag, FArchive& Ar);
#endif

	/** Sets from a ImportText string, used in asset registry */
	void FromExportString(const FString& ExportString, int32 PortFlags = 0);

	/** Handles importing tag strings without (TagName=) in it */
	bool ImportTextItem(const TCHAR*& Buffer, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText);

	/** An empty Message Tag */
	static const FMessageTag EmptyTag;

protected:

	/** Intentionally private so only the tag manager can use */
	explicit FMessageTag(const FName& InTagName);

	/** This Tags Name */
	UPROPERTY(VisibleAnywhere, Category = MessageTags, SaveGame)
	FName TagName;

	friend class UMessageTagsManager;
	friend class FMessageTagRedirectors;
	friend class FNativeMessageTag;
	friend struct FMessageTagContainer;
	friend struct FMessageTagNode;
};

template<>
struct TStructOpsTypeTraits< FMessageTag > : public TStructOpsTypeTraitsBase2< FMessageTag >
{
	enum
	{
		WithNetSerializer = true,
		WithNetSharedSerialization = true,
		WithPostSerialize = true,
#if UE_4_22_OR_LATER
		WithStructuredSerializeFromMismatchedTag = true,
#else
		WithSerializeFromMismatchedTag = true,
#endif
		WithImportTextItem = true,
	};
};

/** A Tag Container holds a collection of FMessageTags, tags are included explicitly by adding them, and implicitly from adding child tags */
USTRUCT(BlueprintType)
struct MESSAGETAGS_API FMessageTagContainer
{
	GENERATED_USTRUCT_BODY()

	/** Constructors */
	FMessageTagContainer()
	{
	}

	FMessageTagContainer(FMessageTagContainer const& Other)
	{
		*this = Other;
	}

	/** Explicit to prevent people from accidentally using the wrong type of operation */
	explicit FMessageTagContainer(const FMessageTag& Tag)
	{
		AddTag(Tag);
	}

	FMessageTagContainer(FMessageTagContainer&& Other)
		: MessageTags(MoveTemp(Other.MessageTags))
		, ParentTags(MoveTemp(Other.ParentTags))
	{

	}

	~FMessageTagContainer()
	{
	}

	/** Creates a container from an array of tags, this is more efficient than adding them all individually */
	template<class AllocatorType>
	static FMessageTagContainer CreateFromArray(const TArray<FMessageTag, AllocatorType>& SourceTags)
	{
		FMessageTagContainer Container;
		Container.MessageTags.Append(SourceTags);
		Container.FillParentTags();
		return Container;
	}

	/** Assignment/Equality operators */
	FMessageTagContainer& operator=(FMessageTagContainer const& Other);
	FMessageTagContainer& operator=(FMessageTagContainer&& Other);
	bool operator==(FMessageTagContainer const& Other) const;
	bool operator!=(FMessageTagContainer const& Other) const;

	/**
	 * Determine if TagToCheck is present in this container, also checking against parent tags
	 * {"A.1"}.HasTag("A") will return True, {"A"}.HasTag("A.1") will return False
	 * If TagToCheck is not Valid it will always return False
	 * 
	 * @return True if TagToCheck is in this container, false if it is not
	 */
	FORCEINLINE_DEBUGGABLE bool HasTag(const FMessageTag& TagToCheck) const
	{
		if (!TagToCheck.IsValid())
		{
			return false;
		}
		// Check explicit and parent tag list 
		return MessageTags.Contains(TagToCheck) || ParentTags.Contains(TagToCheck);
	}

	/**
	 * Determine if TagToCheck is explicitly present in this container, only allowing exact matches
	 * {"A.1"}.HasTagExact("A") will return False
	 * If TagToCheck is not Valid it will always return False
	 * 
	 * @return True if TagToCheck is in this container, false if it is not
	 */
	FORCEINLINE_DEBUGGABLE bool HasTagExact(const FMessageTag& TagToCheck) const
	{
		if (!TagToCheck.IsValid())
		{
			return false;
		}
		// Only check check explicit tag list
		return MessageTags.Contains(TagToCheck);
	}

	/**
	 * Checks if this container contains ANY of the tags in the specified container, also checks against parent tags
	 * {"A.1"}.HasAny({"A","B"}) will return True, {"A"}.HasAny({"A.1","B"}) will return False
	 * If ContainerToCheck is empty/invalid it will always return False
	 *
	 * @return True if this container has ANY of the tags of in ContainerToCheck
	 */
	FORCEINLINE_DEBUGGABLE bool HasAny(const FMessageTagContainer& ContainerToCheck) const
	{
		if (ContainerToCheck.IsEmpty())
		{
			return false;
		}
		for (const FMessageTag& OtherTag : ContainerToCheck.MessageTags)
		{
			if (MessageTags.Contains(OtherTag) || ParentTags.Contains(OtherTag))
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Checks if this container contains ANY of the tags in the specified container, only allowing exact matches
	 * {"A.1"}.HasAny({"A","B"}) will return False
	 * If ContainerToCheck is empty/invalid it will always return False
	 *
	 * @return True if this container has ANY of the tags of in ContainerToCheck
	 */
	FORCEINLINE_DEBUGGABLE bool HasAnyExact(const FMessageTagContainer& ContainerToCheck) const
	{
		if (ContainerToCheck.IsEmpty())
		{
			return false;
		}
		for (const FMessageTag& OtherTag : ContainerToCheck.MessageTags)
		{
			if (MessageTags.Contains(OtherTag))
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Checks if this container contains ALL of the tags in the specified container, also checks against parent tags
	 * {"A.1","B.1"}.HasAll({"A","B"}) will return True, {"A","B"}.HasAll({"A.1","B.1"}) will return False
	 * If ContainerToCheck is empty/invalid it will always return True, because there were no failed checks
	 *
	 * @return True if this container has ALL of the tags of in ContainerToCheck, including if ContainerToCheck is empty
	 */
	FORCEINLINE_DEBUGGABLE bool HasAll(const FMessageTagContainer& ContainerToCheck) const
	{
		if (ContainerToCheck.IsEmpty())
		{
			return true;
		}
		for (const FMessageTag& OtherTag : ContainerToCheck.MessageTags)
		{
			if (!MessageTags.Contains(OtherTag) && !ParentTags.Contains(OtherTag))
			{
				return false;
			}
		}
		return true;
	}

	/**
	 * Checks if this container contains ALL of the tags in the specified container, only allowing exact matches
	 * {"A.1","B.1"}.HasAll({"A","B"}) will return False
	 * If ContainerToCheck is empty/invalid it will always return True, because there were no failed checks
	 *
	 * @return True if this container has ALL of the tags of in ContainerToCheck, including if ContainerToCheck is empty
	 */
	FORCEINLINE_DEBUGGABLE bool HasAllExact(const FMessageTagContainer& ContainerToCheck) const
	{
		if (ContainerToCheck.IsEmpty())
		{
			return true;
		}
		for (const FMessageTag& OtherTag : ContainerToCheck.MessageTags)
		{
			if (!MessageTags.Contains(OtherTag))
			{
				return false;
			}
		}
		return true;
	}

	/** Returns the number of explicitly added tags */
	FORCEINLINE int32 Num() const
	{
		return MessageTags.Num();
	}

	/** Returns whether the container has any valid tags */
	FORCEINLINE bool IsValid() const
	{
		return MessageTags.Num() > 0;
	}

	/** Returns true if container is empty */
	FORCEINLINE bool IsEmpty() const
	{
		return MessageTags.Num() == 0;
	}

	/** Returns a new container explicitly containing the tags of this container and all of their parent tags */
	FMessageTagContainer GetMessageTagParents() const;

	/**
	 * Returns a filtered version of this container, returns all tags that match against any of the tags in OtherContainer, expanding parents
	 *
	 * @param OtherContainer		The Container to filter against
	 *
	 * @return A FMessageTagContainer containing the filtered tags
	 */
	FMessageTagContainer Filter(const FMessageTagContainer& OtherContainer) const;

	/**
	 * Returns a filtered version of this container, returns all tags that match exactly one in OtherContainer
	 *
	 * @param OtherContainer		The Container to filter against
	 *
	 * @return A FMessageTagContainer containing the filtered tags
	 */
	FMessageTagContainer FilterExact(const FMessageTagContainer& OtherContainer) const;

	/** 
	 * Checks if this container matches the given query.
	 *
	 * @param Query		Query we are checking against
	 *
	 * @return True if this container matches the query, false otherwise.
	 */
	bool MatchesQuery(const struct FGameplayTagQuery& Query) const;

	/** 
	 * Adds all the tags from one container to this container 
	 * NOTE: From set theory, this effectively is the union of the container this is called on with Other.
	 *
	 * @param Other TagContainer that has the tags you want to add to this container 
	 */
	void AppendTags(FMessageTagContainer const& Other);

	/** 
	 * Adds all the tags that match between the two specified containers to this container.  WARNING: This matches any
	 * parent tag in A, not just exact matches!  So while this should be the union of the container this is called on with
	 * the intersection of OtherA and OtherB, it's not exactly that.  Since OtherB matches against its parents, any tag
	 * in OtherA which has a parent match with a parent of OtherB will count.  For example, if OtherA has Color.Green
	 * and OtherB has Color.Red, that will count as a match due to the Color parent match!
	 * If you want an exact match, you need to call A.FilterExact(B) (above) to get the intersection of A with B.
	 * If you need the disjunctive union (the union of two sets minus their intersection), use AppendTags to create
	 * Union, FilterExact to create Intersection, and then call Union.RemoveTags(Intersection).
	 *
	 * @param OtherA TagContainer that has the matching tags you want to add to this container, these tags have their parents expanded
	 * @param OtherB TagContainer used to check for matching tags.  If the tag matches on any parent, it counts as a match.
	 */
	void AppendMatchingTags(FMessageTagContainer const& OtherA, FMessageTagContainer const& OtherB);

	/**
	 * Add the specified tag to the container
	 *
	 * @param TagToAdd Tag to add to the container
	 */
	void AddTag(const FMessageTag& TagToAdd);

	/**
	 * Add the specified tag to the container without checking for uniqueness
	 *
	 * @param TagToAdd Tag to add to the container
	 * 
	 * Useful when building container from another data struct (TMap for example)
	 */
	void AddTagFast(const FMessageTag& TagToAdd);

	/**
	 * Adds a tag to the container and removes any direct parents, wont add if child already exists
	 *
	 * @param Tag			The tag to try and add to this container
	 * 
	 * @return True if tag was added
	 */
	bool AddLeafTag(const FMessageTag& TagToAdd);

	/**
	 * Tag to remove from the container
	 * 
	 * @param TagToRemove		Tag to remove from the container
	 * @param bDeferParentTags	Skip calling FillParentTags for performance (must be handled by calling code)
	 */
	bool RemoveTag(const FMessageTag& TagToRemove, bool bDeferParentTags=false);

	/**
	 * Removes all tags in TagsToRemove from this container
	 *
	 * @param TagsToRemove	Tags to remove from the container
	 */
	void RemoveTags(const FMessageTagContainer& TagsToRemove);

	/** Remove all tags from the container. Will maintain slack by default */
	void Reset(int32 Slack = 0);
	
	/** Serialize the tag container */
#if UE_4_24_OR_LATER
	bool Serialize(FStructuredArchive::FSlot Slot);
#else
	bool Serialize(FArchive& Ar);
#endif

	/** Efficient network serialize, takes advantage of the dictionary */
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);

	/** Handles fixup after importing from text */
	bool ImportTextItem(const TCHAR*& Buffer, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText);

	/** Fill in the ParentTags array and any other transient parameters */
	void PostScriptConstruct();

	/** Returns string version of container in ImportText format */
	FString ToString() const;

	/** Sets from a ImportText string, used in asset registry */
	void FromExportString(const FString& ExportString, int32 ExportFlags = 0);

	/** Returns abbreviated human readable Tag list without parens or property names. If bQuoted is true it will quote each tag */
	FString ToStringSimple(bool bQuoted = false) const;

	/** Returns abbreviated human readable Tag list without parens or property names, but will limit each string to specified len.  This is to get around output restrictions*/
	TArray<FString> ToStringsMaxLen(int32 MaxLen) const;

	/** Returns human readable description of what match is being looked for on the readable tag list. */
	FText ToMatchingText(EMessageContainerMatchType MatchType, bool bInvertCondition) const;

	/** Gets the explicit list of message tags */
	void GetMessageTagArray(TArray<FMessageTag>& InOutMessageTags) const
	{
		InOutMessageTags = MessageTags;
	}

	/** Gets the explicit list of message tags */
	const TArray<FMessageTag>& GetMessageTagArray() const;

	/** Creates a const iterator for the contents of this array */
	TArray<FMessageTag>::TConstIterator CreateConstIterator() const
	{
		return MessageTags.CreateConstIterator();
	}

	bool IsValidIndex(int32 Index) const
	{
		return MessageTags.IsValidIndex(Index);
	}

	FMessageTag GetByIndex(int32 Index) const
	{
		if (IsValidIndex(Index))
		{
			return MessageTags[Index];
		}
		return FMessageTag();
	}	

	FMessageTag First() const
	{
		return MessageTags.Num() > 0 ? MessageTags[0] : FMessageTag();
	}

	FMessageTag Last() const
	{
		return MessageTags.Num() > 0 ? MessageTags.Last() : FMessageTag();
	}

	/** Fills in ParentTags from MessageTags */
	void FillParentTags();

	/** An empty Message Tag Container */
	static const FMessageTagContainer EmptyContainer;

PRAGMA_DISABLE_DEPRECATION_WARNINGS

	UE_DEPRECATED(5.0, "Deprecated in favor of HasTag or HasTagExact")
	FORCEINLINE_DEBUGGABLE bool HasTagFast(FMessageTag const& TagToCheck, TEnumAsByte<EMessageTagMatchType::Type> TagMatchType, TEnumAsByte<EMessageTagMatchType::Type> TagToCheckMatchType) const
	{
		bool bResult;
		if (TagToCheckMatchType == EMessageTagMatchType::Explicit)
		{
			// Always check explicit
			bResult = MessageTags.Contains(TagToCheck);

			if (!bResult && TagMatchType == EMessageTagMatchType::IncludeParentTags)
			{
				// Check parent tags as well
				bResult = ParentTags.Contains(TagToCheck);
			}
		}
		else
		{
			bResult = ComplexHasTag(TagToCheck, TagMatchType, TagToCheckMatchType);
		}
		return bResult;
	}

	/**
	 * Determine if the container has the specified tag
	 * 
	 * @param TagToCheck			Tag to check if it is present in the container
	 * @param TagMatchType			Type of match to use for the tags in this container
	 * @param TagToCheckMatchType	Type of match to use for the TagToCheck Param
	 * 
	 * @return True if the tag is in the container, false if it is not
	 */
	UE_DEPRECATED(5.0, "Deprecated in favor of HasTag or HasTagExact")
	bool ComplexHasTag(FMessageTag const& TagToCheck, TEnumAsByte<EMessageTagMatchType::Type> TagMatchType, TEnumAsByte<EMessageTagMatchType::Type> TagToCheckMatchType) const;

	/**
	 * Returns true if the tags in this container match the tags in OtherContainer for the specified matching types.
	 *
	 * @param OtherContainer		The Container to filter against
	 * @param TagMatchType			Type of match to use for the tags in this container
	 * @param OtherTagMatchType		Type of match to use for the tags in the OtherContainer param
	 * @param ContainerMatchType	Type of match to use for filtering
	 *
	 * @return Returns true if ContainerMatchType is Any and any of the tags in OtherContainer match the tags in this or ContainerMatchType is All and all of the tags in OtherContainer match at least one tag in this. Returns false otherwise.
	 */
	UE_DEPRECATED(5.0, "Deprecated in favor of HasAll and related functions")
	FORCEINLINE_DEBUGGABLE bool DoesTagContainerMatch(const FMessageTagContainer& OtherContainer, TEnumAsByte<EMessageTagMatchType::Type> TagMatchType, TEnumAsByte<EMessageTagMatchType::Type> OtherTagMatchType, EMessageContainerMatchType ContainerMatchType) const
	{
		SCOPE_CYCLE_COUNTER(STAT_FMessageTagContainer_DoesTagContainerMatch);
		bool bResult;
		if (OtherTagMatchType == EMessageTagMatchType::Explicit)
		{
			// Start true for all, start false for any
			bResult = (ContainerMatchType == EMessageContainerMatchType::All);
			for (const FMessageTag& OtherTag : OtherContainer.MessageTags)
			{
				if (HasTagFast(OtherTag, TagMatchType, OtherTagMatchType))
				{
					if (ContainerMatchType == EMessageContainerMatchType::Any)
					{
						bResult = true;
						break;
					}
				}
				else if (ContainerMatchType == EMessageContainerMatchType::All)
				{
					bResult = false;
					break;
				}
			}
		}
		else
		{
			FMessageTagContainer OtherExpanded = OtherContainer.GetMessageTagParents();
			return DoesTagContainerMatch(OtherExpanded, TagMatchType, EMessageTagMatchType::Explicit, ContainerMatchType);
		}
		return bResult;
	}

PRAGMA_ENABLE_DEPRECATION_WARNINGS

protected:

	/**
	 * If a Tag with the specified tag name explicitly exists, it will remove that tag and return true.  Otherwise, it 
	   returns false.  It does NOT check the TagName for validity (i.e. the tag could be obsolete and so not exist in
	   the table). It also does NOT check parents (because it cannot do so for a tag that isn't in the table).
	   NOTE: This function should ONLY ever be used by MessageTagsManager when redirecting tags.  Do NOT make this
	   function public!
	 */
	bool RemoveTagByExplicitName(const FName& TagName);

	/** Adds parent tags for a single tag */
	void AddParentsForTag(const FMessageTag& Tag);

	/** Array of message tags */
	UPROPERTY(BlueprintReadWrite, Category=MessageTags, SaveGame)
	TArray<FMessageTag> MessageTags;

	/** Array of expanded parent tags, in addition to MessageTags. Used to accelerate parent searches. May contain duplicates in some cases */
	UPROPERTY(Transient)
	TArray<FMessageTag> ParentTags;

	friend class UMessageTagsManager;
	friend class FMessageTagRedirectors;
#if 0
	friend struct FMessageTagQuery;
	friend struct FMessageTagQueryExpression;
#endif
	friend struct FMessageTagNode;
	friend struct FMessageTag;
	
private:

	/**
	 * DO NOT USE DIRECTLY
	 * STL-like iterators to enable range-based for loop support.
	 */
	
	FORCEINLINE friend TArray<FMessageTag>::TConstIterator begin(const FMessageTagContainer& Array) { return Array.CreateConstIterator(); }
	FORCEINLINE friend TArray<FMessageTag>::TConstIterator end(const FMessageTagContainer& Array) { return TArray<FMessageTag>::TConstIterator(Array.MessageTags, Array.MessageTags.Num()); }
};

FORCEINLINE bool FMessageTag::MatchesAnyExact(const FMessageTagContainer& ContainerToCheck) const
{
	if (ContainerToCheck.IsEmpty())
	{
		return false;
	}
	return ContainerToCheck.MessageTags.Contains(*this);
}

template<>
struct TStructOpsTypeTraits<FMessageTagContainer> : public TStructOpsTypeTraitsBase2<FMessageTagContainer>
{
	enum
	{
#if UE_4_24_OR_LATER
		WithStructuredSerializer = true,
#else
		WithSerializer = true,
#endif
		WithIdenticalViaEquality = true,
		WithNetSerializer = true,
		WithNetSharedSerialization = true,
		WithImportTextItem = true,
		WithCopy = true,
		WithPostScriptConstruct = true,
	};
#if UE_5_00_OR_LATER
	static constexpr EPropertyObjectReferenceType WithSerializerObjectReferences = EPropertyObjectReferenceType::None;
#endif
};

/** Class that can be subclassed by a game/plugin to allow easily adding native Message tags at startup */
struct MESSAGETAGS_API FMessageTagNativeAdder
{
	FMessageTagNativeAdder();

	virtual void AddTags() = 0;
};

/** Helper struct: drop this in another struct to get an embedded create new tag widget. */
USTRUCT()
struct FMessageTagCreationWidgetHelper
{
	GENERATED_USTRUCT_BODY()
};
