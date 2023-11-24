// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#if 1
#include "Containers/Set.h"
#include "Containers/UnrealString.h"
#include "CoreMinimal.h"
#include "CoreTypes.h"
#include "MessageTagContainer.h"
#include "MessageTagsManager.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/NameTypes.h"

enum class ENativeMessageTagToken { PRIVATE_USE_MACRO_INSTEAD };

namespace UE::MessageTags::Private
{
	// Used to prevent people from putting UE_DEFINE_MESSAGE_TAG_STATIC and UE_DEFINE_MESSAGE_TAG in their headers.
	constexpr bool HasFileExtension(const char* File)
	{
		const char* It = File;
		while (*It)
			++It;
		return It[-1] == 'p' && It[-2] == 'p' && It[-3] == 'c' && It[-4] == '.';
	}
}

/**
 * Declares a native Message tag that is defined in a cpp with UE_DEFINE_MESSAGE_TAG to allow other modules or code to use the created tag variable.
 */
#define UE_DECLARE_MESSAGE_TAG_EXTERN(TagName) extern FNativeMessageTag TagName;

/**
 * Defines a native gameplay tag that is externally declared in a header to allow other modules or code to use the created tag variable.
 */
#define UE_DEFINE_MESSAGE_TAG(TagName, Tag)                                                                              \
	FNativeMessageTag TagName(UE_PLUGIN_NAME, UE_MODULE_NAME, Tag, TEXT(""), ENativeMessageTagToken::PRIVATE_USE_MACRO_INSTEAD); \
	static_assert(UE::MessageTags::Private::HasFileExtension(__FILE__, ".cpp"),                                          \
				  "UE_DEFINE_MESSAGE_TAG can only be used in .cpp files, if you're trying to share tags across modules, use UE_DECLARE_MESSAGE_TAG_EXTERN in the public header, and UE_DEFINE_MESSAGE_TAG in the private .cpp");

/**
 * Defines a native gameplay tag such that it's only available to the cpp file you define it in.
 */
#define UE_DEFINE_MESSAGE_TAG_STATIC(TagName, Tag)                                                                              \
	static FNativeMessageTag TagName(UE_PLUGIN_NAME, UE_MODULE_NAME, Tag, TEXT(""), ENativeMessageTagToken::PRIVATE_USE_MACRO_INSTEAD); \
	static_assert(UE::MessageTags::Private::HasFileExtension(__FILE__, ".cpp"),                                                 \
				  "UE_DEFINE_MESSAGE_TAG_STATIC can only be used in .cpp files, if you're trying to share tags across modules, use UE_DECLARE_MESSAGE_TAG_EXTERN in the public header, and UE_DEFINE_MESSAGE_TAG in the private .cpp");

#ifndef UE_INCLUDE_NATIVE_MESSAGETAG_METADATA
	#define UE_INCLUDE_NATIVE_MESSAGETAG_METADATA WITH_EDITOR && !UE_BUILD_SHIPPING
#endif
/**
 * Holds a gameplay tag that was registered during static construction of the module, and will
 * be unregistered when the module unloads.  Each registration is based on the native tag pointer
 * so even if two modules register the same tag and one is unloaded, the tag will still be registered
 * by the other one.
 */
class MESSAGETAGS_API FNativeMessageTag : public FNoncopyable
{
public:
	static FName NAME_NativeMessageTag;

public:
	FNativeMessageTag(FName PluginName, FName ModuleName, FName TagName, const FString& TagDevComment, ENativeMessageTagToken);
	~FNativeMessageTag();

	operator FMessageTag() const { return InternalTag; }

	FMessageTag GetTag() const { return InternalTag; }

	FMessageTagTableRow GetMessageTagTableRow() const
	{
#if !UE_BUILD_SHIPPING
		ValidateTagRegistration();
#endif

#if WITH_EDITORONLY_DATA
		return FMessageTagTableRow(InternalTag.GetTagName(), DeveloperComment);
#else
		return FMessageTagTableRow(InternalTag.GetTagName());
#endif
	}

#if UE_INCLUDE_NATIVE_MESSAGETAG_METADATA
	FName GetPlugin() const { return PluginName; }
	FName GetModuleName() const { return ModuleName; }
	FName GetModulePackageName() const { return ModulePackageName; }
#else
	FName GetModuleName() const { return NAME_NativeGameplayTag; }
	FName GetPlugin() const { return NAME_None; }
	FName GetModulePackageName() const { return NAME_None; }
#endif

private:
	FMessageTag InternalTag;

#if !UE_BUILD_SHIPPING
	FName PluginName;
	FName ModuleName;
	FName ModulePackageName;
	mutable bool bValidated = false;

	void ValidateTagRegistration() const;
#endif

#if WITH_EDITORONLY_DATA
	FString DeveloperComment;
#endif

	static TSet<const class FNativeMessageTag*>& GetRegisteredNativeTags();

	friend class UMessageTagsManager;
};
#endif