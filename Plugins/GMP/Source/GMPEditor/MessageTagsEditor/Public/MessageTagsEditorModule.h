// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Misc/EngineVersionComparison.h"

class IPropertyTypeCustomization;
struct FMessageTag;
struct FMessageTagContainer;
struct FMessageTagContainerCustomizationOptions;
struct FMessageTagCustomizationOptions;
struct FMessageParameter;

DECLARE_DELEGATE_OneParam(FOnSetMessageTag, const FMessageTag&);
DECLARE_DELEGATE_OneParam(FOnSetMessageTagContainer, const FMessageTagContainer&);

class SWidget;

/**
 * The public interface to this module
 */
class IMessageTagsEditorModule : public IModuleInterface
{

public:

	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IMessageTagsEditorModule& Get()
	{
		return FModuleManager::LoadModuleChecked< IMessageTagsEditorModule >( "MessageTagsEditor" );
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded( "MessageTagsEditor" );
	}

	/** Tries to add a new message tag to the ini lists */
	MESSAGETAGSEDITOR_API virtual bool AddNewMessageTagToINI(const FString& NewTag,
															 const FString& Comment = TEXT(""),
															 FName TagSourceName = NAME_None,
															 bool bIsRestrictedTag = false,
															 bool bAllowNonRestrictedChildren = true,
															 const TArray<FMessageParameter>& Parameters = {},
															 const TArray<FMessageParameter>& ResponseTypes = {}) = 0;

	/** Tries to delete a tag from the library. This will pop up special UI or error messages as needed. It will also delete redirectors if that is specified. */
	MESSAGETAGSEDITOR_API virtual bool DeleteTagFromINI(TSharedPtr<struct FMessageTagNode> TagNodeToDelete) = 0;
	MESSAGETAGSEDITOR_API virtual void DeleteTagsFromINI(const TArray<TSharedPtr<struct FMessageTagNode>>& TagNodesToDelete) = 0;

	/** Tries to rename a tag, leaving a rediretor in the ini, and adding the new tag if it does not exist yet */
	MESSAGETAGSEDITOR_API virtual bool RenameTagInINI(const FString& TagToRename, const FString& TagToRenameTo, const TArray<FMessageParameter>& Parameters, const TArray<FMessageParameter>& ResponseTypes) = 0;

	/** Updates info about a tag */
	MESSAGETAGSEDITOR_API virtual bool UpdateTagInINI(const FString& TagToUpdate, const FString& Comment, bool bIsRestrictedTag, bool bAllowNonRestrictedChildren) = 0;

	/** Tries to move existing tags from each source ini lists to the target source ini list. */
	MESSAGETAGSEDITOR_API virtual bool MoveTagsBetweenINI(const TArray<FString>& TagsToMove, const FName& TargetTagSource, TArray<FString>& OutTagsMoved, TArray<FString>& OutFailedToMoveTags) = 0;

	/** Adds a transient message tag (only valid for the current editor session) */
	MESSAGETAGSEDITOR_API virtual bool AddTransientEditorMessageTag(const FString& NewTransientTag) = 0;

	/** Adds a new tag source, well use project config directory if not specified. This will not save anything until a tag is added */
	MESSAGETAGSEDITOR_API virtual bool AddNewMessageTagSource(const FString& NewTagSource, const FString& RootDirToUse = FString()) = 0;

	/**
	 * Creates a simple version of a tag container widget that has a default value and will call a custom callback
	 * @param OnSetTag			Delegate called when container is changed
	 * @param MessageTagValue	Shared ptr to tag container value that will be used as default and modified
	 * @param FilterString		Optional filter string, same format as Categories metadata on tag properties
	 */
	MESSAGETAGSEDITOR_API virtual TSharedRef<SWidget> MakeMessageTagContainerWidget(FOnSetMessageTagContainer OnSetTag, TSharedPtr<FMessageTagContainer> MessageTagContainer, const FString& FilterString = FString()) = 0;

	/**
	 * Creates a simple version of a gameplay tag widget that has a default value and will call a custom callback
	 * @param OnSetTag			Delegate called when tag is changed
	 * @param MessageTagValue	Shared ptr to tag value that will be used as default and modified
	 * @param FilterString		Optional filter string, same format as Categories metadata on tag properties
	 */
	MESSAGETAGSEDITOR_API virtual TSharedRef<SWidget> MakeMessageTagWidget(FOnSetMessageTag OnSetTag, TSharedPtr<FMessageTag> MessageTag, const FString& FilterString = FString()) = 0;
	/** Returns the list of gameplay tags that are not used by content */
	MESSAGETAGSEDITOR_API virtual void GetUnusedMessageTags(TArray<TSharedPtr<struct FMessageTagNode>>& OutUnusedTags) = 0;
};

/** This is public so that child structs of FMessageTag can use the details customization */
struct MESSAGETAGSEDITOR_API FMessageTagCustomizationPublic
{
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
#if UE_VERSION_OLDER_THAN(5, 2, 0)
	static TSharedRef<IPropertyTypeCustomization> MakeInstanceWithOptions(const FMessageTagCustomizationOptions& Options);
#endif
};

/** This is public so that child structs of FGameplayTagContainer can use the details customization */
struct MESSAGETAGSEDITOR_API FMessageTagContainerCustomizationPublic
{
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
#if UE_VERSION_OLDER_THAN(5, 2, 0)
	static TSharedRef<IPropertyTypeCustomization> MakeInstanceWithOptions(const FMessageTagContainerCustomizationOptions& Options);
#endif
};

struct MESSAGETAGSEDITOR_API FRestrictedMessageTagCustomizationPublic
{
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
};

#if defined(UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2) && UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#include "MessageTagContainer.h"
#include "MessageTagContainerCustomizationOptions.h"
#include "MessageTagCustomizationOptions.h"
#include "IPropertyTypeCustomization.h"
#endif
