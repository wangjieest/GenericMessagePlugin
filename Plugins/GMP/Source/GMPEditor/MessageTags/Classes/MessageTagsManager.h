// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/ScriptMacros.h"
#include "MessageTagContainer.h"
#include "Engine/DataTable.h"
#include "Templates/UniquePtr.h"
#include "Misc/ScopeLock.h"
#include "UnrealCompatibility.h"
#if WITH_EDITOR && UE_5_06_OR_LATER
#include "Misc/TransactionallySafeCriticalSection.h"
#include "Hash/Blake3.h"
#endif

#include "MessageTagsManager.generated.h"

class UMessageTagsList;
struct FStreamableHandle;
class FNativeMessageTag;

#if WITH_EDITOR && UE_5_06_OR_LATER
namespace UE::Cook { class FCookDependency; }
namespace UE::Cook { class ICookInfo; }
#endif

USTRUCT(BlueprintInternalUseOnly)
struct FMessageParameter
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = MessageTag)
	FName Name;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = MessageTag)
	FName Type;
};

/** Simple struct for a table row in the message tag table and element in the ini list */
USTRUCT()
struct MESSAGETAGS_API FMessageTagTableRow : public FTableRowBase
{
	GENERATED_USTRUCT_BODY()

	/** Tag specified in the table */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=MessageTag)
	FName Tag;

	/** Developer comment clarifying the usage of a particular tag, not user facing */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=MessageTag)
	FString DevComment;

	/** Constructors */
	FMessageTagTableRow() {}

	UPROPERTY(EditAnywhere, Category = MessageTag)
	TArray<FMessageParameter> Parameters;

	UPROPERTY(EditAnywhere, Category = MessageTag)
	TArray<FMessageParameter> ResponseTypes;

	FMessageTagTableRow(FName InTag, const FString& InDevComment = TEXT(""), const TArray<FMessageParameter>& InParameters = {}, const TArray<FMessageParameter>& InResTypes = {})
		: Tag(InTag)
		, DevComment(InDevComment)
		, Parameters(InParameters)
		, ResponseTypes(InResTypes)
	{
	}

	FMessageTagTableRow(FMessageTagTableRow const& Other);

	/** Assignment/Equality operators */
	FMessageTagTableRow& operator=(FMessageTagTableRow const& Other);
	bool operator==(FMessageTagTableRow const& Other) const;
	bool operator!=(FMessageTagTableRow const& Other) const;
	bool operator<(FMessageTagTableRow const& Other) const;
};

/** Simple struct for a table row in the restricted message tag table and element in the ini list */
USTRUCT()
struct MESSAGETAGS_API FRestrictedMessageTagTableRow : public FMessageTagTableRow
{
	GENERATED_USTRUCT_BODY()

	/** Tag specified in the table */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = MessageTag)
	bool bAllowNonRestrictedChildren;

	/** Constructors */
	FRestrictedMessageTagTableRow() : bAllowNonRestrictedChildren(false) {}
	FRestrictedMessageTagTableRow(FName InTag, const FString& InDevComment = TEXT(""), bool InAllowNonRestrictedChildren = false, const TArray<FMessageParameter>& InParameters = {}, const TArray<FMessageParameter>& InResTypes = {})
		: FMessageTagTableRow(InTag, InDevComment, InParameters, InResTypes)
		, bAllowNonRestrictedChildren(InAllowNonRestrictedChildren)
	{
	}
	FRestrictedMessageTagTableRow(FRestrictedMessageTagTableRow const& Other);

	/** Assignment/Equality operators */
	FRestrictedMessageTagTableRow& operator=(FRestrictedMessageTagTableRow const& Other);
	bool operator==(FRestrictedMessageTagTableRow const& Other) const;
	bool operator!=(FRestrictedMessageTagTableRow const& Other) const;
};

UENUM()
enum class EMessageTagSourceType : uint8
{
	Native,				// Was added from C++ code
	DefaultTagList,		// The default tag list in DefaultMessageTags.ini
	TagList,			// Another tag list from an ini in tags/*.ini
	RestrictedTagList,	// Restricted tags from an ini
	DataTable,			// From a DataTable
	Invalid,			// Not a real source
};

UENUM()
enum class EMessageTagSelectionType : uint8
{
	None,
	NonRestrictedOnly,
	RestrictedOnly,
	All
};

/** Struct defining where message tags are loaded/saved from. Mostly for the editor */
USTRUCT()
struct MESSAGETAGS_API FMessageTagSource
{
	GENERATED_USTRUCT_BODY()

	/** Name of this source */
	UPROPERTY()
	FName SourceName;

	/** Type of this source */
	UPROPERTY()
	EMessageTagSourceType SourceType;

	/** If this is bound to an ini object for saving, this is the one */
	UPROPERTY()
	class UMessageTagsList* SourceTagList;

	/** If this has restricted tags and is bound to an ini object for saving, this is the one */
	UPROPERTY()
	class URestrictedMessageTagsList* SourceRestrictedTagList;

	FMessageTagSource() 
		: SourceName(NAME_None), SourceType(EMessageTagSourceType::Invalid), SourceTagList(nullptr), SourceRestrictedTagList(nullptr)
	{
	}

	FMessageTagSource(FName InSourceName, EMessageTagSourceType InSourceType, UMessageTagsList* InSourceTagList = nullptr, URestrictedMessageTagsList* InSourceRestrictedTagList = nullptr) 
		: SourceName(InSourceName), SourceType(InSourceType), SourceTagList(InSourceTagList), SourceRestrictedTagList(InSourceRestrictedTagList)
	{
	}

	/** Returns the config file that created this source, if valid */
	FString GetConfigFileName() const;

	static FName GetNativeName();

	static FName GetDefaultName();

#if WITH_EDITOR
	static FName GetFavoriteName();

	static void SetFavoriteName(FName TagSourceToFavorite);

	static FName GetTransientEditorName();
#endif
};

/** Struct describing the places to look for ini search paths */
struct FMessageTagSearchPathInfo
{
	/** Which sources should be loaded from this path */
	TArray<FName> SourcesInPath;

	/** Config files to load from, will normally correspond to FoundSources */
	TArray<FString> TagIniList;

	/** True if this path has already been searched */
	bool bWasSearched = false;

	/** True if the tags in sources have been added to the current tree */
	bool bWasAddedToTree = false;

	FORCEINLINE void Reset()
	{
		SourcesInPath.Reset();
		TagIniList.Reset();
		bWasSearched = false;
		bWasAddedToTree = false;
	}

	FORCEINLINE bool IsValid()
	{
		return bWasSearched && bWasAddedToTree;
	}
};

/** Simple tree node for message tags, this stores metadata about specific tags */
USTRUCT()
struct FMessageTagNode
{
	GENERATED_USTRUCT_BODY()
	FMessageTagNode(){};

	/** Simple constructor, passing redundant data for performance */
	FMessageTagNode(FName InTag, FName InFullTag, TSharedPtr<FMessageTagNode> InParentNode, bool InIsExplicitTag, bool InIsRestrictedTag, bool InAllowNonRestrictedChildren);

	/** Returns a correctly constructed container with only this tag, useful for doing container queries */
	FORCEINLINE const FMessageTagContainer& GetSingleTagContainer() const { return CompleteTagWithParents; }

	/**
	 * Get the complete tag for the node, including all parent tags, delimited by periods
	 * 
	 * @return Complete tag for the node
	 */
	FORCEINLINE const FMessageTag& GetCompleteTag() const { return CompleteTagWithParents.Num() > 0 ? CompleteTagWithParents.MessageTags[0] : FMessageTag::EmptyTag; }
	FORCEINLINE FName GetCompleteTagName() const { return GetCompleteTag().GetTagName(); }
	FORCEINLINE FString GetCompleteTagString() const { return GetCompleteTag().ToString(); }

	/**
	 * Get the simple tag for the node (doesn't include any parent tags)
	 * 
	 * @return Simple tag for the node
	 */
	FORCEINLINE FName GetSimpleTagName() const { return Tag; }

	/**
	 * Get the children nodes of this node
	 * 
	 * @return Reference to the array of the children nodes of this node
	 */
	FORCEINLINE TArray< TSharedPtr<FMessageTagNode> >& GetChildTagNodes() { return ChildTags; }

	/**
	 * Get the children nodes of this node
	 * 
	 * @return Reference to the array of the children nodes of this node
	 */
	FORCEINLINE const TArray< TSharedPtr<FMessageTagNode> >& GetChildTagNodes() const { return ChildTags; }

	/**
	 * Get the parent tag node of this node
	 * 
	 * @return The parent tag node of this node
	 */
	FORCEINLINE TSharedPtr<FMessageTagNode> GetParentTagNode() const { return ParentNode; }

	/**
	* Get the net index of this node
	*
	* @return The net index of this node
	*/
	FORCEINLINE FMessageTagNetIndex GetNetIndex() const {  check(NetIndex != INVALID_TAGNETINDEX); return NetIndex; }

	/** Reset the node of all of its values */
	MESSAGETAGS_API void ResetNode();

	/** Returns true if the tag was explicitly specified in code or data */
	FORCEINLINE bool IsExplicitTag() const {
#if WITH_EDITORONLY_DATA
		return bIsExplicitTag;
#else
		return true;
#endif
	}

	/** Returns true if the tag is a restricted tag and allows non-restricted children */
	FORCEINLINE bool GetAllowNonRestrictedChildren() const { 
#if WITH_EDITORONLY_DATA
		return bAllowNonRestrictedChildren;  
#else
		return true;
#endif
	}

	/** Returns true if the tag is a restricted tag */
	FORCEINLINE bool IsRestrictedMessageTag() const {
#if WITH_EDITORONLY_DATA
		return bIsRestrictedTag;
#else
		return true;
#endif
	}

#if WITH_EDITORONLY_DATA
	FName GetFirstSourceName() const { return SourceNames.Num() == 0 ? NAME_None : SourceNames[0]; }
	const TArray<FName>& GetAllSourceNames() const { return SourceNames; }
#endif

	TArray<FMessageParameter> Parameters;
	FORCEINLINE const FString& GetDevComment() const
	{
#if WITH_EDITORONLY_DATA
		return DevComment;
#else
		static FString EmptyString;
		return EmptyString;
#endif
	}
	TArray<FMessageParameter> ResponseTypes;

#if WITH_EDITOR && UE_5_06_OR_LATER
	/**
	 * Update the hasher with a deterministic hash of the data on this. Used for e.g. IncrementalCook keys.
	 * Does not include data from this node's child or parent nodes.
	 */
	void Hash(FBlake3& Hasher);
#endif

private:
	/** Raw name for this tag at current rank in the tree */
	FName Tag;

	/** This complete tag is at MessageTags[0], with parents in ParentTags[] */
	FMessageTagContainer CompleteTagWithParents;

	/** Child message tag nodes */
	TArray< TSharedPtr<FMessageTagNode> > ChildTags;

	/** Owner message tag node, if any */
	TSharedPtr<FMessageTagNode> ParentNode;
	
	/** Net Index of this node */
	FMessageTagNetIndex NetIndex;

#if WITH_EDITORONLY_DATA
	/** Module or Package or config file this tag came from. If empty this is an implicitly added tag */
	TArray<FName> SourceNames;

	/** Comment for this tag */
	FString DevComment;

	/** If this is true then the tag can only have normal tag children if bAllowNonRestrictedChildren is true */
	uint8 bIsRestrictedTag : 1;

	/** If this is true then any children of this tag must come from the restricted tags */
	uint8 bAllowNonRestrictedChildren : 1;

	/** If this is true then the tag was explicitly added and not only implied by its child tags */
	uint8 bIsExplicitTag : 1;

	/** If this is true then at least one tag that inherits from this tag is coming from multiple sources. Used for updating UI in the editor. */
	uint8 bDescendantHasConflict : 1;

	/** If this is true then this tag is coming from multiple sources. No descendants can be changed on this tag until this is resolved. */
	uint8 bNodeHasConflict : 1;

	/** If this is true then at least one tag that this tag descends from is coming from multiple sources. This tag and it's descendants can't be changed in the editor. */
	uint8 bAncestorHasConflict : 1;
#endif 

	friend class UMessageTagsManager;
	friend class SMessageTagWidget;
	friend class SMessageTagPicker;
};

/** Holds data about the tag dictionary, is in a singleton UObject */
UCLASS(config=Engine)
class MESSAGETAGS_API UMessageTagsManager : public UObject
{
	GENERATED_UCLASS_BODY()

	/** Destructor */
	~UMessageTagsManager();

	/** Returns the global UMessageTagsManager manager */
	FORCEINLINE static UMessageTagsManager& Get()
	{
		if (SingletonManager == nullptr)
		{
			InitializeManager();
		}

		return *SingletonManager;
	}

	/** Returns possibly nullptr to the manager. Needed for some shutdown cases to avoid reallocating. */
	FORCEINLINE static UMessageTagsManager* GetIfAllocated() { return SingletonManager; }

	/**
	* Adds the message tags corresponding to the strings in the array TagStrings to OutTagsContainer
	*
	* @param TagStrings Array of strings to search for as tags to add to the tag container
	* @param OutTagsContainer Container to add the found tags to.
	* @param ErrorIfNotfound: ensure() that tags exists.
	*
	*/
	void RequestMessageTagContainer(const TArray<FString>& TagStrings, FMessageTagContainer& OutTagsContainer, bool bErrorIfNotFound=true) const;

	/**
	 * Gets the FMessageTag that corresponds to the TagName
	 *
	 * @param TagName The Name of the tag to search for
	 * @param ErrorIfNotfound: ensure() that tag exists.
	 * 
	 * @return Will return the corresponding FMessageTag or an empty one if not found.
	 */
	FMessageTag RequestMessageTag(FName TagName, bool ErrorIfNotFound=true) const;

	/** 
	 * Returns true if this is a valid message tag string (foo.bar.baz). If false, it will fill 
	 * @param TagString String to check for validity
	 * @param OutError If non-null and string invalid, will fill in with an error message
	 * @param OutFixedString If non-null and string invalid, will attempt to fix. Will be empty if no fix is possible
	 * @return True if this can be added to the tag dictionary, false if there's a syntax error
	 */
	bool IsValidMessageTagString(const TCHAR* TagString, FText* OutError = nullptr, FString* OutFixedString = nullptr);
	bool IsValidMessageTagString(const FString& TagString, FText* OutError = nullptr, FString* OutFixedString = nullptr);
	bool IsValidMessageTagString(const FStringView& TagString, FText* OutError = nullptr, FStringBuilderBase* OutFixedString = nullptr);

	/**
	 *	Searches for a message tag given a partial string. This is slow and intended mainly for console commands/utilities to make
	 *	developer life's easier. This will attempt to match as best as it can. If you pass "A.b" it will match on "A.b." before it matches "a.b.c".
	 */
	FMessageTag FindMessageTagFromPartialString_Slow(FString PartialString) const;

	/**
	 * Registers the given name as a message tag, and tracks that it is being directly referenced from code
	 * This can only be called during engine initialization, the table needs to be locked down before replication
	 *
	 * @param TagName The Name of the tag to add
	 * @param TagDevComment The developer comment clarifying the usage of the tag
	 * 
	 * @return Will return the corresponding FMessageTag
	 */
	FMessageTag AddNativeMessageTag(FName TagName, const FString& TagDevComment = TEXT("(Native)"));

private:
	// Only callable from FNativeMessageTag, these functions do less error checking and can happen after initial tag loading is done
	void AddNativeMessageTag(FNativeMessageTag* TagSource);
	void RemoveNativeMessageTag(const FNativeMessageTag* TagSource);

public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnMessageTagSignatureChanged, FName);
	static FOnMessageTagSignatureChanged& OnMessageTagSignatureChanged();

	/** Call to flush the list of native tags, once called it is unsafe to add more */
	void DoneAddingNativeTags();

	static FSimpleMulticastDelegate& OnLastChanceToAddNativeTags();

	/**
	 * Register a callback for when native tags are done being added (this is also a safe point to consider that the Message tags have fully been initialized).
	 * Or, if the native tags have already been added (and thus we have registered all valid tags), then execute this Delegate immediately.
	 * This is useful if your code is potentially executed during load time, and therefore any tags in your block of code could be not-yet-loaded, but possibly valid after being loaded.
	 */
	FDelegateHandle CallOrRegister_OnDoneAddingNativeTagsDelegate(const FSimpleMulticastDelegate::FDelegate& Delegate) const;

	/**
	 * Gets a Tag Container containing the supplied tag and all of its parents as explicit tags.
	 * For example, passing in x.y.z would return a tag container with x.y.z, x.y, and x.
	 * This will only work for tags that have been properly registered.
	 *
	 * @param MessageTag The Tag to use at the child most tag for this container
	 * 
	 * @return A Tag Container with the supplied tag and all its parents added explicitly
	 */
	FMessageTagContainer RequestMessageTagParents(const FMessageTag& MessageTag) const;

	/**
	 * Fills in an array of message tags with all of tags that are the parents of the passed in tag.
	 * For example, passing in x.y.z would add x.y and x to UniqueParentTags if they was not already there.
	 * This is used by the MessageTagContainer code and may work for unregistered tags depending on serialization settings.
	 *
	 * @param MessageTag The message tag to extract parent tags from
	 * @param UniqueParentTags A list of parent tags that will be added to if necessary
	 *
	 * @return true if any tags were added to UniqueParentTags
	 */
	bool ExtractParentTags(const FMessageTag& MessageTag, TArray<FMessageTag>& UniqueParentTags) const;

	/**
	 * Gets a Tag Container containing the all tags in the hierarchy that are children of this tag. Does not return the original tag
	 *
	 * @param MessageTag					The Tag to use at the parent tag
	 * 
	 * @return A Tag Container with the supplied tag and all its parents added explicitly
	 */
	FMessageTagContainer RequestMessageTagChildren(const FMessageTag& MessageTag) const;

	/** Returns direct parent MessageTag of this MessageTag, calling on x.y will return x */
	FMessageTag RequestMessageTagDirectParent(const FMessageTag& MessageTag) const;

	/**
	UE_DEPRECATED(5.4, "This function is not threadsafe, use FindTagNode or FMessageTag::GetSingleTagContainer")
	FORCEINLINE_DEBUGGABLE const FMessageTagContainer* GetSingleTagContainer(const FMessageTag& MessageTag) const
	{
		return GetSingleTagContainerPtr(MessageTag);
	}
	*/
	/**
	 * Checks node tree to see if a FMessageTagNode with the tag exists
	 *
	 * @param TagName	The name of the tag node to search for
	 *
	 * @return A shared pointer to the FMessageTagNode found, or NULL if not found.
	 */
	FORCEINLINE_DEBUGGABLE TSharedPtr<FMessageTagNode> FindTagNode(const FMessageTag& MessageTag) const
	{
#if UE_5_06_OR_LATER
		UE::TScopeLock Lock(MessageTagMapCritical);
#else
		FScopeLock Lock(&MessageTagMapCritical);
#endif
		const TSharedPtr<FMessageTagNode>* Node = MessageTagNodeMap.Find(MessageTag);

		if (Node)
		{
			return *Node;
		}
#if WITH_EDITOR
		// Check redirector
		if (GIsEditor && MessageTag.IsValid())
		{
			FMessageTag RedirectedTag = MessageTag;

			RedirectSingleMessageTag(RedirectedTag, nullptr);

			Node = MessageTagNodeMap.Find(RedirectedTag);

			if (Node)
			{
				return *Node;
			}
		}
#endif
		return nullptr;
	}

	/**
	 * Checks node tree to see if a FMessageTagNode with the name exists
	 *
	 * @param TagName	The name of the tag node to search for
	 *
	 * @return A shared pointer to the FMessageTagNode found, or NULL if not found.
	 */
	FORCEINLINE_DEBUGGABLE TSharedPtr<FMessageTagNode> FindTagNode(FName TagName) const
	{
		FMessageTag PossibleTag(TagName);
		return FindTagNode(PossibleTag);
	}

	/** Loads the tag tables referenced in the MessageTagSettings object */
	void LoadMessageTagTables(bool bAllowAsyncLoad = false);

	/** Loads tag inis contained in the specified path */
	void AddTagIniSearchPath(const FString& RootDir);

	/** Tries to remove the specified search path, will return true if anything was removed */
	bool RemoveTagIniSearchPath(const FString& RootDir);

	/** Gets all the current directories to look for tag sources in */
	void GetTagSourceSearchPaths(TArray<FString>& OutPaths);

	/** Gets the number of tag source search paths */
	int32 GetNumTagSourceSearchPaths();

	/** Helper function to construct the message tag tree */
	void ConstructMessageTagTree();

	/** Helper function to destroy the message tag tree */
	void DestroyMessageTagTree();

	/** Splits a tag such as x.y.z into an array of names {x,y,z} */
	void SplitMessageTagFName(const FMessageTag& Tag, TArray<FName>& OutNames) const;

	/** Gets the list of all tags in the dictionary */
	void RequestAllMessageTags(FMessageTagContainer& TagContainer, bool OnlyIncludeDictionaryTags) const;

	/** Returns true if if the passed in name is in the tag dictionary and can be created */
	bool ValidateTagCreation(FName TagName) const;

	/** Returns the tag source for a given tag source name and type, or null if not found */
	const FMessageTagSource* FindTagSource(FName TagSourceName) const;

	/** Returns the tag source for a given tag source name and type, or null if not found */
	FMessageTagSource* FindTagSource(FName TagSourceName);

	/** Fills in an array with all tag sources of a specific type */
	void FindTagSourcesWithType(EMessageTagSourceType TagSourceType, TArray<const FMessageTagSource*>& OutArray) const;

	void FindTagsWithSource(FStringView PackageNameOrPath, TArray<FMessageTag>& OutTags) const;

	/**
	 * Check to see how closely two FMessageTags match. Higher values indicate more matching terms in the tags.
	 *
	 * @param MessageTagOne	The first tag to compare
	 * @param MessageTagTwo	The second tag to compare
	 *
	 * @return the length of the longest matching pair
	 */
	int32 MessageTagsMatchDepth(const FMessageTag& MessageTagOne, const FMessageTag& MessageTagTwo) const;

	/** Returns the number of parents a particular Message tag has.  Useful as a quick way to determine which tags may
	 * be more "specific" than other tags without comparing whether they are in the same hierarchy or anything else.
	 * Example: "TagA.SubTagA" has 2 Tag Nodes.  "TagA.SubTagA.LeafTagA" has 3 Tag Nodes.
	 */ 
	int32 GetNumberOfTagNodes(const FMessageTag& MessageTag) const;

	/** Returns true if we should import tags from UMessageTagsSettings objects (configured by INI files) */
	bool ShouldImportTagsFromINI() const;

	/** Should we print loading errors when trying to load invalid tags */
	bool ShouldWarnOnInvalidTags() const
	{
		return bShouldWarnOnInvalidTags;
	}

	/** Should we clear references to invalid tags loaded/saved in the editor */
	UE_DEPRECATED(5.5, "We should never clear invalid tags as we're not guaranteed the required plugin has loaded")
	bool ShouldClearInvalidTags() const
	{
		return false;
	}

	/** Should use fast replication */
	bool ShouldUseFastReplication() const
	{
		return bUseFastReplication;
	}

	/** Should use dynamic replication (Message Tags need not match between client/server) */
	bool ShouldUseDynamicReplication() const
	{
		return !bUseFastReplication && bUseDynamicReplication;
	}

	/** If we are allowed to unload tags */
	bool ShouldUnloadTags() const;

	/** Pushes an override that supersedes bShouldAllowUnloadingTags to allow/disallow unloading of MessageTags in controlled scenarios */
	void SetShouldUnloadTagsOverride(bool bShouldUnloadTags);

	/** Clears runtime overrides, reverting to bShouldAllowUnloadingTags when determining MessageTags unload behavior */
	void ClearShouldUnloadTagsOverride();

	/** Pushes an override that suppresses calls to HandleMessageTagTreeChanged that would result in a complete rebuild of the MessageTag tree */
	void SetShouldDeferMessageTagTreeRebuilds(bool bShouldDeferRebuilds);

	/** Stops suppressing MessageTag tree rebuilds and (optionally) rebuilds the tree */
	void ClearShouldDeferMessageTagTreeRebuilds(bool bRebuildTree);

	/** Returns the hash of NetworkMessageTagNodeIndex */
	uint32 GetNetworkMessageTagNodeIndexHash() const { VerifyNetworkIndex(); return NetworkMessageTagNodeIndexHash; }

	/** Returns a list of the ini files that contain restricted tags */
	void GetRestrictedTagConfigFiles(TArray<FString>& RestrictedConfigFiles) const;

	/** Returns a list of the source files that contain restricted tags */
	void GetRestrictedTagSources(TArray<const FMessageTagSource*>& Sources) const;

	/** Returns a list of the owners for a restricted tag config file. May be empty */
	void GetOwnersForTagSource(const FString& SourceName, TArray<FString>& OutOwners) const;

	/** Notification that a tag container has been loaded via serialize */
	void MessageTagContainerLoaded(FMessageTagContainer& Container, FProperty* SerializingProperty) const;

	/** Notification that a message tag has been loaded via serialize */
	void SingleMessageTagLoaded(FMessageTag& Tag, FProperty* SerializingProperty) const;

	/** Handles redirectors for an entire container, will also error on invalid tags */
	void RedirectTagsForContainer(FMessageTagContainer& Container, FProperty* SerializingProperty) const;

	/** Handles redirectors for a single tag, will also error on invalid tag. This is only called for when individual tags are serialized on their own */
	void RedirectSingleMessageTag(FMessageTag& Tag, FProperty* SerializingProperty) const;

	/** Handles establishing a single tag from an imported tag name (accounts for redirects too). Called when tags are imported via text. */
	bool ImportSingleMessageTag(FMessageTag& Tag, FName ImportedTagName, bool bImportFromSerialize = false) const;

	/** Gets a tag name from net index and vice versa, used for replication efficiency */
	FName GetTagNameFromNetIndex(FMessageTagNetIndex Index) const;
	FMessageTagNetIndex GetNetIndexFromTag(const FMessageTag &InTag) const;

	/** Cached number of bits we need to replicate tags. That is, Log2(Number of Tags). Will always be <= 16. */
	int32 GetNetIndexTrueBitNum() const { VerifyNetworkIndex(); return NetIndexTrueBitNum; }

	/** The length in bits of the first segment when net serializing tags. We will serialize NetIndexFirstBitSegment + 1 bit to indicatore "more" (more = second segment that is NetIndexTrueBitNum - NetIndexFirstBitSegment) */
	int32 GetNetIndexFirstBitSegment() const { VerifyNetworkIndex(); return NetIndexFirstBitSegment; }

	/** This is the actual value for an invalid tag "None". This is computed at runtime as (Total number of tags) + 1 */
	FMessageTagNetIndex GetInvalidTagNetIndex() const { VerifyNetworkIndex(); return InvalidTagNetIndex; }

	const TArray<TSharedPtr<FMessageTagNode>>& GetNetworkMessageTagNodeIndex() const { VerifyNetworkIndex(); return NetworkMessageTagNodeIndex; }

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnMessageTagLoaded, const FMessageTag& /*Tag*/)
	FOnMessageTagLoaded OnMessageTagLoadedDelegate;

	/** Numbers of bits to use for replicating container size. This can be set via config. */
	int32 NumBitsForContainerSize;

	void PushDeferOnMessageTagTreeChangedBroadcast();
	void PopDeferOnMessageTagTreeChangedBroadcast();

private:
	/** Cached number of bits we need to replicate tags. That is, Log2(Number of Tags). Will always be <= 16. */
	int32 NetIndexTrueBitNum;

	/** The length in bits of the first segment when net serializing tags. We will serialize NetIndexFirstBitSegment + 1 bit to indicatore "more" (more = second segment that is NetIndexTrueBitNum - NetIndexFirstBitSegment) */
	int32 NetIndexFirstBitSegment;

	/** This is the actual value for an invalid tag "None". This is computed at runtime as (Total number of tags) + 1 */
	FMessageTagNetIndex InvalidTagNetIndex;

public:

#if WITH_EDITOR
	/** Gets a Filtered copy of the MessageRootTags Array based on the comma delimited filter string passed in */
	void GetFilteredMessageRootTags(const FString& InFilterString, TArray< TSharedPtr<FMessageTagNode> >& OutTagArray) const;

	/** Returns "Categories" meta property from given handle, used for filtering by tag widget */
	FString GetCategoriesMetaFromPropertyHandle(TSharedPtr<class IPropertyHandle> PropertyHandle) const;

	/** Helper function, made to be called by custom OnGetCategoriesMetaFromPropertyHandle handlers  */
	static FString StaticGetCategoriesMetaFromPropertyHandle(TSharedPtr<class IPropertyHandle> PropertyHandle);

	/** Returns "Categories" meta property from given field, used for filtering by tag widget */
	template <typename TFieldType>
	static FString GetCategoriesMetaFromField(TFieldType* Field)
	{
		check(Field);
		if (Field->HasMetaData(NAME_Categories))
		{
			return Field->GetMetaData(NAME_Categories);
		}
		else if (Field->HasMetaData(NAME_MessageTagFilter))
		{
			return Field->GetMetaData(NAME_MessageTagFilter);
		}
		return FString();
	}

	/** Returns "MessageTagFilter" meta property from given function, used for filtering by tag widget for any parameters of the function that end up as BP pins */
	FString GetCategoriesMetaFromFunction(const UFunction* Func, FName ParamName = NAME_None) const;

	/** Gets a list of all message tag nodes added by the specific source */
	void GetAllTagsFromSource(FName TagSource, TArray< TSharedPtr<FMessageTagNode> >& OutTagArray) const;

	/** Returns true if this tag is directly in the dictionary already */
	bool IsDictionaryTag(FName TagName) const;

	/** Returns information about tag. If not found return false */
	bool GetTagEditorData(FName TagName, FString& OutComment, FName &OutTagSource, bool& bOutIsTagExplicit, bool &bOutIsRestrictedTag, bool &bOutAllowNonRestrictedChildren) const;
	
	/** Returns information about tag. If not found return false */
    bool GetTagEditorData(FName TagName, FString& OutComment, TArray<FName>& OutTagSources, bool& bOutIsTagExplicit, bool &bOutIsRestrictedTag, bool &bOutAllowNonRestrictedChildren) const;

	/** This is called after EditorRefreshMessageTagTree. Useful if you need to do anything editor related when tags are added or removed */
	static FSimpleMulticastDelegate OnEditorRefreshMessageTagTree;

	/** Refresh the MessageTag tree due to an editor change */
	void EditorRefreshMessageTagTree();

	/** Suspends EditorRefreshMessageTagTree requests */
	void SuspendEditorRefreshMessageTagTree(FGuid SuspendToken);

	/** Resumes EditorRefreshMessageTagTree requests; triggers a refresh if a request was made while it was suspended */
	void ResumeEditorRefreshMessageTagTree(FGuid SuspendToken);

	/** Gets a Tag Container containing all of the tags in the hierarchy that are children of this tag, and were explicitly added to the dictionary */
	FMessageTagContainer RequestMessageTagChildrenInDictionary(const FMessageTag& MessageTag) const;
#if WITH_EDITORONLY_DATA
	/** Gets a Tag Container containing all of the tags in the hierarchy that are children of this tag, were explicitly added to the dictionary, and do not have any explicitly added tags between them and the specified tag */
	FMessageTagContainer RequestMessageTagDirectDescendantsInDictionary(const FMessageTag& MessageTag, EMessageTagSelectionType SelectionType) const;
#endif // WITH_EDITORONLY_DATA


	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMessageTagDoubleClickedEditor, FMessageTag, FSimpleMulticastDelegate& /* OUT */)
	FOnMessageTagDoubleClickedEditor OnGatherMessageTagDoubleClickedEditor;

	/** Chance to dynamically change filter string based on a property handle */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnGetCategoriesMetaFromPropertyHandle, TSharedPtr<IPropertyHandle>, FString& /* OUT */)
	FOnGetCategoriesMetaFromPropertyHandle OnGetCategoriesMetaFromPropertyHandle;

	/** Allows dynamic hiding of message tags in SMessageTagWidget. Allows higher order structs to dynamically change which tags are visible based on its own data */
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnFilterMessageTagChildren, const FString&  /** FilterString */, TSharedPtr<FMessageTagNode>& /* TagNode */, bool& /* OUT OutShouldHide */)
	FOnFilterMessageTagChildren OnFilterMessageTagChildren;

	/*
	* This is a container to filter out Message tags when they are invalid or when they don't meet the filter string
	* If used from editor to filter out tags when picking them the FilterString is optional and the ReferencingPropertyHandle is required
	* If used to validate an asset / assets you can provide the TagSourceAssets. The FilterString and ReferencingPropertyHandle is optional
	*/
	struct FFilterMessageTagContext
	{
		const FString& FilterString;
		const TSharedPtr<FMessageTagNode>& TagNode;
		const FMessageTagSource* TagSource;
		const TSharedPtr<IPropertyHandle>& ReferencingPropertyHandle;
		const TArray<FAssetData> TagSourceAssets;

		FFilterMessageTagContext(const FString& InFilterString, const TSharedPtr<FMessageTagNode>& InTagNode, const FMessageTagSource* InTagSource, const TSharedPtr<IPropertyHandle>& InReferencingPropertyHandle)
			: FilterString(InFilterString), TagNode(InTagNode), TagSource(InTagSource), ReferencingPropertyHandle(InReferencingPropertyHandle)
		{}

		//FFilterGameplayTagContext(const TSharedPtr<FGameplayTagNode>& InTagNode, const FGameplayTagSource* InTagSource, const TArray<FAssetData>& InTagSourceAssets, const FString& InFilterString = FString())
		//	: FilterString(InFilterString), TagNode(InTagNode), TagSource(InTagSource), TagSourceAssets(InTagSourceAssets)
		//{}
	};

	/*
	 * Allows dynamic hiding of Message tags in SMessageTagWidget. Allows higher order structs to dynamically change which tags are visible based on its own data
	 * Applies to all tags, and has more context than OnFilterMessageTagChildren
	 */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnFilterMessageTag, const FFilterMessageTagContext& /** InContext */, bool& /* OUT OutShouldHide */)
	FOnFilterMessageTag OnFilterMessageTag;
	
	void NotifyMessageTagDoubleClickedEditor(FString TagName);
	
	bool ShowMessageTagAsHyperLinkEditor(FString TagName);

	/**
	 * Used for incremental cooking. Create an FCookDependency that reports tags that have been read from ini.
	 * Packages that pass this dependency to AddCookLoadDependency or AddCookSaveDependency in their OnCookEvent or
	 * (if Ar.IsCooking()) Serialize function will be invalidated and recooked by the incremental cook whenever those
	 * tags change.
	 */
#if UE_5_06_OR_LATER
	UE::Cook::FCookDependency CreateCookDependency();
#endif
	/** Implementation of console command MessageTags.DumpSources */
	void DumpSources(FOutputDevice& Out) const;
#endif //WITH_EDITOR

	void PrintReplicationIndices();
	int32 GetNumMessageTagNodes() const { return MessageTagNodeMap.Num(); }

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	/** Mechanism for tracking what tags are frequently replicated */

	void PrintReplicationFrequencyReport();
	void NotifyTagReplicated(FMessageTag Tag, bool WasInContainer);

	TMap<FMessageTag, int32>	ReplicationCountMap;
	TMap<FMessageTag, int32>	ReplicationCountMap_SingleTags;
	TMap<FMessageTag, int32>	ReplicationCountMap_Containers;
#endif

private:

	/** Initializes the manager */
	static void InitializeManager();

	/** finished loading/adding native tags */
	static FSimpleMulticastDelegate& OnDoneAddingNativeTagsDelegate();

	/** The Tag Manager singleton */
	static UMessageTagsManager* SingletonManager;

	friend class FMessageTagTest;
	friend class FMessageEffectsTest;
	friend class FMessageTagsModule;
	friend class FMessageTagsEditorModule;
	friend class UMessageTagsSettings;
	friend class SAddNewMessageTagSourceWidget;
	friend class FNativeMessageTag;

	/**
	 * Helper function to get the stored TagContainer containing only this tag, which has searchable ParentTags
	 * NOTE: This function is not threadsafe and should only be used in code that locks the tag map critical section
	 * @param MessageTag		Tag to get single container of
	 * @return					Pointer to container with this tag
	 */
	FORCEINLINE_DEBUGGABLE const FMessageTagContainer* GetSingleTagContainerPtr(const FMessageTag& MessageTag) const
	{
		// Doing this with pointers to avoid a shared ptr reference count change
		const TSharedPtr<FMessageTagNode>* Node = MessageTagNodeMap.Find(MessageTag);

		if (Node)
		{
			return &(*Node)->GetSingleTagContainer();
		}
#if WITH_EDITOR
		// Check redirector
		if (GIsEditor && MessageTag.IsValid())
		{
			FMessageTag RedirectedTag = MessageTag;

			RedirectSingleMessageTag(RedirectedTag, nullptr);

			Node = MessageTagNodeMap.Find(RedirectedTag);

			if (Node)
			{
				return &(*Node)->GetSingleTagContainer();
			}
		}
#endif
		return nullptr;
	}


	/**
	 * Helper function to insert a tag into a tag node array
	 *
	 * @param Tag							Short name of tag to insert
	 * @param FullTag						Full tag, passed in for performance
	 * @param ParentNode					Parent node, if any, for the tag
	 * @param NodeArray						Node array to insert the new node into, if necessary (if the tag already exists, no insertion will occur)
	 * @param SourceName					File tag was added from
	 * @param DevComment					Comment from developer about this tag
	 * @param bIsExplicitTag				Is the tag explicitly defined or is it implied by the existence of a child tag
	 * @param bIsRestrictedTag				Is the tag a restricted tag or a regular message tag
	 * @param bAllowNonRestrictedChildren	If the tag is a restricted tag, can it have regular message tag children or should all of its children be restricted tags as well?
	 *
	 * @return Index of the node of the tag
	 */
	int32 InsertTagIntoNodeArray(FName Tag, FName FullTag, TSharedPtr<FMessageTagNode> ParentNode, TArray< TSharedPtr<FMessageTagNode> >& NodeArray, FName SourceName, const FMessageTagTableRow& TagRow, bool bIsExplicitTag, bool bIsRestrictedTag, bool bAllowNonRestrictedChildren);

	/** Helper function to populate the tag tree from each table */
	void PopulateTreeFromDataTable(class UDataTable* Table);

	void AddTagTableRow(const FMessageTagTableRow& TagRow, FName SourceName, bool bIsRestrictedTag = false, bool bAllowNonRestrictedChildren = true);

	void AddChildrenTags(FMessageTagContainer& TagContainer, TSharedPtr<FMessageTagNode> MessageTagNode, bool RecurseAll=true, bool OnlyIncludeDictionaryTags=false) const;

	void AddRestrictedMessageTagSource(const FString& FileName);

	void AddTagsFromAdditionalLooseIniFiles(const TArray<FString>& IniFileList);

	/**
	 * Helper function for MessageTagsMatch to get all parents when doing a parent match,
	 * NOTE: Must never be made public as it uses the FNames which should never be exposed
	 * 
	 * @param NameList		The list we are adding all parent complete names too
	 * @param MessageTag	The current Tag we are adding to the list
	 */
	void GetAllParentNodeNames(TSet<FName>& NamesList, TSharedPtr<FMessageTagNode> MessageTag) const;

	/** Returns the tag source for a given tag source name, or null if not found */
	FMessageTagSource* FindOrAddTagSource(FName TagSourceName, EMessageTagSourceType SourceType, const FString& RootDirToUse= FString());

	/** Constructs the net indices for each tag */
	void ConstructNetIndex();

	/** Marks all of the nodes that descend from CurNode as having an ancestor node that has a source conflict. */
	void MarkChildrenOfNodeConflict(TSharedPtr<FMessageTagNode> CurNode);

	void VerifyNetworkIndex() const
	{
		if (!bUseFastReplication)
		{
			UE_LOG(LogMessageTags, Warning, TEXT("%hs called when not using FastReplication (not rebuilding the fast replication cache)"), __func__);
		}
		else if (bNetworkIndexInvalidated)
		{
			const_cast<UMessageTagsManager*>(this)->ConstructNetIndex();
		}
	}

	void InvalidateNetworkIndex() { bNetworkIndexInvalidated = true; }

	/** Called in both editor and game when the tag tree changes during startup or editing */
	void BroadcastOnMessageTagTreeChanged();

	/** Call after modifying the tag tree nodes, this will either call the full editor refresh or a limited game refresh */
	void HandleMessageTagTreeChanged(bool bRecreateTree);

#if WITH_EDITOR && UE_5_06_OR_LATER
	void UpdateIncrementalCookHash(UE::Cook::ICookInfo& CookInfo);
#endif

	// Tag Sources
	///////////////////////////////////////////////////////

	/** These are the old native tags that use to be resisted via a function call with no specific site/ownership. */
	TSet<FName> LegacyNativeTags;

	/** Map of all config directories to load tag inis from */
	TMap<FString, FMessageTagSearchPathInfo> RegisteredSearchPaths;

	/** Roots of message tag nodes */
	TSharedPtr<FMessageTagNode> MessageRootTag;

	/** Map of Tags to Nodes - Internal use only. FMessageTag is inside node structure, do not use FindKey! */
	TMap<FMessageTag, TSharedPtr<FMessageTagNode>> MessageTagNodeMap;

	void SyncToGMPMeta();

	/** Our aggregated, sorted list of commonly replicated tags. These tags are given lower indices to ensure they replicate in the first bit segment. */
	TArray<FMessageTag> CommonlyReplicatedTags;

	/** Map of message tag source names to source objects */
	UPROPERTY()
	TMap<FName, FMessageTagSource> TagSources;

	TSet<FName> RestrictedMessageTagSourceNames;

	bool bIsConstructingMessageTagTree = false;

	/** Cached runtime value for whether we are using fast replication or not. Initialized from config setting. */
	bool bUseFastReplication;

	/** Cached runtime value for whether we are using dynamic replication or not. Initialized from the config setting. */
	bool bUseDynamicReplication;

	/** Cached runtime value for whether we should warn when loading invalid tags */
	bool bShouldWarnOnInvalidTags;

	/** Cached runtime value for whether we should allow unloading of tags */
	bool bShouldAllowUnloadingTags;

	/** Augments usage of bShouldAllowUnloadingTags to allow runtime overrides to allow/disallow unloading of MessageTags in controlled scenarios */
	TOptional<bool> ShouldAllowUnloadingTagsOverride;

	/** Used to suppress calls to HandleMessageTagTreeChanged that would result in a complete rebuild of the MessageTag tree*/
	TOptional<bool> ShouldDeferMessageTagTreeRebuilds;

	/** True if native tags have all been added and flushed */
	bool bDoneAddingNativeTags;

	int32 bDeferBroadcastOnMessageTagTreeChanged = 0;
	bool bShouldBroadcastDeferredOnMessageTagTreeChanged = false;

	/** If true, an action that would require a tree rebuild was performed during initialization **/
	bool bNeedsTreeRebuildOnDoneAddingMessageTags = false;

	/** String with outlawed characters inside tags */
	FString InvalidTagCharacters;

	// This critical section is to handle an issue where tag requests come from another thread when async loading from a background thread in FMessageTagContainer::Serialize.
	// This class is not generically threadsafe.
#if UE_5_06_OR_LATER
	mutable FTransactionallySafeCriticalSection MessageTagMapCritical;
#else
	mutable FCriticalSection MessageTagMapCritical;
#endif

#if WITH_EDITOR
	// Transient editor-only tags to support quick-iteration PIE workflows
	TSet<FName> TransientEditorTags;

	TSet<FGuid> EditorRefreshMessageTagTreeSuspendTokens;
	bool bEditorRefreshMessageTagTreeRequestedDuringSuspend = false;

#if UE_5_06_OR_LATER
	FBlake3Hash IncrementalCookHash;
#endif
#endif //if WITH_EDITOR

	/** Sorted list of nodes, used for network replication */
	TArray<TSharedPtr<FMessageTagNode>> NetworkMessageTagNodeIndex;

	uint32 NetworkMessageTagNodeIndexHash;

	bool bNetworkIndexInvalidated = true;

	/** Holds all of the valid message-related tags that can be applied to assets */
	UPROPERTY()
	TArray<UDataTable*> MessageTagTables;

	const static FName NAME_Categories;
	const static FName NAME_MessageTagFilter;

	friend class UMessageTagsManagerIncrementalCookFunctions;
};
