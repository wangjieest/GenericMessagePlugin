// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#if 0
#include "CoreMinimal.h"
#include "MessageTagContainer.h"
#include "MessageTagsManager.h"
#include <string>

enum class ENativeMessageTagToken { PRIVATE_USE_MACRO_INSTEAD };

namespace UE::MessageTags::Private
{
	// Used to prevent people from putting UE_DEFINE_GAMEPLAY_TAG_STATIC and UE_DEFINE_GAMEPLAY_TAG in their headers.
	constexpr bool HasFileExtension(std::string_view file, std::string_view file_ext)
	{
		const auto _Rightsize = file_ext.length();
		if (file.length() < _Rightsize) {
			return false;
		}
		return file.compare((file.length() - _Rightsize), _Rightsize, file_ext) == 0;
	}
}

/**
 * Declares a native Message tag that is defined in a cpp with UE_DEFINE_MESSAGE_TAG to allow other modules or code to use the created tag variable.
 */
#define UE_DECLARE_MESSAGE_TAG_EXTERN(TagName) extern FNativeMessageTag TagName;

/**
 * Defines a native gameplay tag that is externally declared in a header to allow other modules or code to use the created tag variable.
 */
#define UE_DEFINE_MESSAGE_TAG(TagName, Tag) FNativeMessageTag TagName(UE_PLUGIN_NAME, UE_MODULE_NAME, Tag, TEXT(""), ENativeMessageTagToken::PRIVATE_USE_MACRO_INSTEAD); static_assert(UE::MessageTags::Private::HasFileExtension(__FILE__, ".cpp"), "UE_DEFINE_MESSAGE_TAG can only be used in .cpp files, if you're trying to share tags across modules, use UE_DECLARE_MESSAGE_TAG_EXTERN in the public header, and UE_DEFINE_MESSAGE_TAG in the private .cpp");

/**
 * Defines a native gameplay tag such that it's only available to the cpp file you define it in.
 */
#define UE_DEFINE_MESSAGE_TAG_STATIC(TagName, Tag) static FNativeMessageTag TagName(UE_PLUGIN_NAME, UE_MODULE_NAME, Tag, TEXT(""), ENativeMessageTagToken::PRIVATE_USE_MACRO_INSTEAD); static_assert(UE::MessageTags::Private::HasFileExtension(__FILE__, ".cpp"), "UE_DEFINE_MESSAGE_TAG_STATIC can only be used in .cpp files, if you're trying to share tags across modules, use UE_DECLARE_MESSAGE_TAG_EXTERN in the public header, and UE_DEFINE_MESSAGE_TAG in the private .cpp");

/**
 * Holds a gameplay tag that was registered during static construction of the module, and will
 * be unregistered when the module unloads.  Each registration is based on the native tag pointer
 * so even if two modules register the same tag and one is unloaded, the tag will still be registered
 * by the other one.
 */
class MESSAGETAGS_API FNativeMessageTag : public FNoncopyable
{
public:
	FNativeMessageTag(FName PluginName, FName ModuleName, FName TagName, const FString& TagDevComment, ENativeMessageTagToken);
	~FNativeMessageTag();

	operator FMessageTag() const { return InternalTag; }

	FMessageTag GetTag() const { return InternalTag; }

	FMessageTagTableRow GetMessageTagTableRow() const
	{
#if WITH_EDITORONLY_DATA
		return FMessageTagTableRow(InternalTag.GetTagName(), DeveloperComment);
#else
		return FMessageTagTableRow(InternalTag.GetTagName());
#endif
	}

private:
	FMessageTag InternalTag;

#if WITH_EDITORONLY_DATA
	FString DeveloperComment;
#endif

	static TSet<const class FNativeMessageTag*>& GetRegisteredNativeTags();

	friend class UMessageTagsManager;
};
#endif