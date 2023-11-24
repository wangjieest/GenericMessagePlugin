// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPath.h"
#include "MessageTagsManager.h"
#include "Engine/DeveloperSettings.h"
#include "MessageTagRedirectors.h"
#include "MessageTagsSettings.generated.h"

/** Category remapping. This allows base engine tag category meta data to remap to multiple project-specific categories. */
USTRUCT()
struct MESSAGETAGS_API FMessageTagCategoryRemap
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = MessageTags)
	FString BaseCategory;

	UPROPERTY(EditAnywhere, Category = MessageTags)
	TArray<FString> RemapCategories;

	friend inline bool operator==(const FMessageTagCategoryRemap& A, const FMessageTagCategoryRemap& B)
	{
		return A.BaseCategory == B.BaseCategory && A.RemapCategories == B.RemapCategories;
	}
};

/** Base class for storing a list of message tags as an ini list. This is used for both the central list and additional lists */
UCLASS(config = MessageTagsList, notplaceable)
class MESSAGETAGS_API UMessageTagsList : public UObject
{
	GENERATED_UCLASS_BODY()

	/** Relative path to the ini file that is backing this list */
	UPROPERTY()
	FString ConfigFileName;

	/** List of tags saved to this file */
	UPROPERTY(config, EditAnywhere, Category = MessageTags)
	TArray<FMessageTagTableRow> MessageTagList;

	/** Sorts tags alphabetically */
	void SortTags();
};

/** Base class for storing a list of restricted message tags as an ini list. This is used for both the central list and additional lists */
UCLASS(config = MessageTags, notplaceable)
class MESSAGETAGS_API URestrictedMessageTagsList : public UObject
{
	GENERATED_UCLASS_BODY()

	/** Relative path to the ini file that is backing this list */
	UPROPERTY()
	FString ConfigFileName;

	/** List of restricted tags saved to this file */
	UPROPERTY(config, EditAnywhere, Category = MessageTags)
	TArray<FRestrictedMessageTagTableRow> RestrictedMessageTagList;

	/** Sorts tags alphabetically */
	void SortTags();
};

USTRUCT()
struct MESSAGETAGS_API FRestrictedMessageCfg
{
	GENERATED_BODY()

	/** Allows new tags to be saved into their own INI file. This is make merging easier for non technical developers by setting up their own ini file. */
	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category = MessageTags)
	FString RestrictedConfigName;

	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category = MessageTags)
	TArray<FString> Owners;

	bool operator==(const FRestrictedMessageCfg& Other) const;
	bool operator!=(const FRestrictedMessageCfg& Other) const;
};

/**
 *	Class for importing MessageTags directly from a config file.
 *	FMessageTagsEditorModule::StartupModule adds this class to the Project Settings menu to be edited.
 *	Editing this in Project Settings will output changes to Config/DefaultMessageTags.ini.
 *	
 *	Primary advantages of this approach are:
 *	-Adding new tags doesn't require checking out external and editing file (CSV or xls) then reimporting.
 *	-New tags are mergeable since .ini are text and non exclusive checkout.
 *	
 *	To do:
 *	-Better support could be added for adding new tags. We could match existing tags and autocomplete subtags as
 *	the user types (e.g, autocomplete 'Damage.Physical' as the user is adding a 'Damage.Physical.Slash' tag).
 *	
 */
UCLASS(config=MessageTags, defaultconfig, notplaceable)
class MESSAGETAGS_API UMessageTagsSettings : public UMessageTagsList
{
	GENERATED_UCLASS_BODY()

	/** If true, will import tags from ini files in the config/tags folder */
	UPROPERTY(config, EditAnywhere, Category = MessageTags)
	bool ImportTagsFromConfig;

	/** If true, will give load warnings when reading in saved tag references that are not in the dictionary */
	UPROPERTY(config, EditAnywhere, Category = MessageTags)
	bool WarnOnInvalidTags;

	/** If true, will clear any invalid tags when reading in saved tag references that are not in the dictionary */
	UPROPERTY(config, EditAnywhere, Category = MessageTags, meta = (ConfigRestartRequired = true))
	bool ClearInvalidTags;

	/** If true, will allow unloading of tags in the editor when plugins are removed */
	UPROPERTY(config, EditAnywhere, Category = "Advanced Message Tags")
	bool AllowEditorTagUnloading;

	/** If true, will allow unloading of tags in a non-editor gebuild when plugins are removed, this is potentially unsafe and affects requests to unload during play in editor */
	UPROPERTY(config, EditAnywhere, Category = "Advanced Message Tags")
	bool AllowGameTagUnloading;

	/** If true, will replicate message tags by index instead of name. For this to work, tags must be identical on client and server */
	UPROPERTY(config, EditAnywhere, Category = "Advanced Replication")
	bool FastReplication;

	/** These characters cannot be used in message tags, in addition to special ones like newline*/
	UPROPERTY(config, EditAnywhere, Category = MessageTags)
	FString InvalidTagCharacters;

	/** Category remapping. This allows base engine tag category meta data to remap to multiple project-specific categories. */
	UPROPERTY(config, EditAnywhere, Category = MessageTags)
	TArray<FMessageTagCategoryRemap> CategoryRemapping;

	/** List of data tables to load tags from */
	UPROPERTY(config, EditAnywhere, Category = MessageTags, meta = (AllowedClasses = "DataTable"))
	TArray<FSoftObjectPath> MessageTagTableList;

	/** List of active tag redirects */
	UPROPERTY(config, EditAnywhere, Category = MessageTags)
	TArray<FMessageTagRedirect> MessageTagRedirects;

	/** List of most frequently replicated tags */
	UPROPERTY(config, EditAnywhere, Category = "Advanced Replication")
	TArray<FName> CommonlyReplicatedTags;

	/** Numbers of bits to use for replicating container size, set this based on how large your containers tend to be */
	UPROPERTY(config, EditAnywhere, Category = "Advanced Replication")
	int32 NumBitsForContainerSize;

	/** The length in bits of the first segment when net serializing tags. We will serialize NetIndexFirstBitSegment + 1 bit to indicate "more", which is slower to replicate */
	UPROPERTY(config, EditAnywhere, Category= "Advanced Replication")
	int32 NetIndexFirstBitSegment;

	/** A list of .ini files used to store restricted message tags. */
	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category = "Advanced Message Tags")
	TArray<FRestrictedMessageCfg> RestrictedConfigFiles;
#if WITH_EDITORONLY_DATA
	// Dummy parameter used to hook the editor UI
	/** Restricted Message Tags.
	 * 
	 *  Restricted tags are intended to be top level tags that are important for your data hierarchy and modified by very few people.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, transient, Category = "Advanced Message Tags")
	FString RestrictedTagList;

	/** Add a new message tag config file for saving plugin or game-specific tags. */
	UPROPERTY(EditAnywhere, transient, Category = "MessageTags")
	FString NewTagSource;
#endif

#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyThatWillChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

private:
	// temporary copy of RestrictedConfigFiles that we use to identify changes in the list
	// this is required to autopopulate the owners field
	TArray<FRestrictedMessageCfg> RestrictedConfigFilesTempCopy;
#endif
};

UCLASS(config=EditorPerProjectUserSettings, meta=(DisplayName="Message Tag Editing"))
class MESSAGETAGS_API UMessageTagsDeveloperSettings : public UDeveloperSettings
{
	GENERATED_UCLASS_BODY()

	virtual FName GetCategoryName() const override;

	/** Allows new tags to be saved into their own INI file. This is make merging easier for non technical developers by setting up their own ini file. */
	UPROPERTY(config, EditAnywhere, Category=MessageTags)
	FString DeveloperConfigName;

	/** Stores the favorite tag source, used as the default ini when adding new tags, can be toggled on/off using the button next to the tag source picker */
	UPROPERTY(config, EditAnywhere, Category=MessageTags)
	FName FavoriteTagSource;
};
