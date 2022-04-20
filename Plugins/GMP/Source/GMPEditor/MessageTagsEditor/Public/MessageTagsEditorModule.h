// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "IPropertyTypeCustomization.h"
#include "MessageTagsManager.h"


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

	/** Tries to rename a tag, leaving a rediretor in the ini, and adding the new tag if it does not exist yet */
	MESSAGETAGSEDITOR_API virtual bool RenameTagInINI(const FString& TagToRename, const FString& TagToRenameTo, const TArray<FMessageParameter>& Parameters, const TArray<FMessageParameter>& ResponseTypes) = 0;

	/** Updates info about a tag */
	MESSAGETAGSEDITOR_API virtual bool UpdateTagInINI(const FString& TagToUpdate, const FString& Comment, bool bIsRestrictedTag, bool bAllowNonRestrictedChildren) = 0;

	/** Adds a transient message tag (only valid for the current editor session) */
	MESSAGETAGSEDITOR_API virtual bool AddTransientEditorMessageTag(const FString& NewTransientTag) = 0;
};

/** This is public so that child structs of FMessageTag can use the details customization */
struct MESSAGETAGSEDITOR_API FMessageTagCustomizationPublic
{
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
};

struct MESSAGETAGSEDITOR_API FRestrictedMessageTagCustomizationPublic
{
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
};