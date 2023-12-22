// Copyright Epic Games, Inc. All Rights Reserved.
#if 1

#include "NativeMessageTags.h"
#include "Interfaces/IProjectManager.h"
#include "ModuleDescriptor.h"
#include "ProjectDescriptor.h"
#include "PluginDescriptor.h"
#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "FNativeMessageTag"

#if !UE_BUILD_SHIPPING

//FEditorDelegates

static bool VerifyModuleCanContainMessageTag(FName ModuleName, FName TagName, const FModuleDescriptor* Module, TSharedPtr<IPlugin> OptionalOwnerPlugin)
{
	if (Module)
	{
		if (!(Module->Type == EHostType::Runtime || Module->Type == EHostType::RuntimeAndProgram))
		{
			// TODO NDarnell - If it's not a module we load always we need to make sure the tag is available in some other fashion
			// such as through an ini.

			//TSharedPtr<IPlugin> ThisPlugin = IPluginManager::Get().FindPlugin(TEXT("CommonDialogue"));
			//check(ThisPlugin.IsValid());
			//UMessageTagsManager::Get().AddTagIniSearchPath(ThisPlugin->GetBaseDir() / TEXT("Config") / TEXT("Tags"));

			//const FString PluginFolder = FPaths::GetPath(StateProperties.PluginInstalledFilename);
			//UMessageTagsManager::Get().AddTagIniSearchPath(PluginFolder / TEXT("Config") / TEXT("Tags"));


			//const FString& PluginDescriptorFilename = Plugin->GetDescriptorFileName();

			//// Make sure you are in the game feature plugins folder. All GameFeaturePlugins are in this folder.
			////@TODO: GameFeaturePluginEnginePush: Comments elsewhere allow plugins outside of the folder as long as they explicitly opt in, either those are wrong or this check is wrong
			//if (!PluginDescriptorFilename.IsEmpty() && FPaths::ConvertRelativePathToFull(PluginDescriptorFilename).StartsWith(GetDefault<UGameFeaturesSubsystemSettings>()->BuiltInGameFeaturePluginsFolder) && FPaths::FileExists(PluginDescriptorFilename))
			//{

			ensureAlwaysMsgf(false, TEXT("Native Message Tag '%s' defined in '%s'.  The module type is '%s' but needs to be 'Runtime' or 'RuntimeAndProgram'.  Client and Server tags must match."), *TagName.ToString(), *ModuleName.ToString(), EHostType::ToString(Module->Type));
		}

		// Not a mistake - we return true even if it fails the test, the return value is a validation we were able to verify that it could or could not.
		return true;
	}

	return false;
}

#endif

FNativeMessageTag::FNativeMessageTag(FName InPluginName, FName InModuleName, FName TagName, const FString& TagDevComment, ENativeMessageTagToken)
{
	// TODO NDarnell To try and make sure nobody is using these during non-static init
	// of the module, we could add an indicator on the module manager indicating
	// if we're actively loading model and make sure we only run this code during
	// that point.

#if !UE_BUILD_SHIPPING
	PluginName = InPluginName;
	ModuleName = InModuleName;
	FString ModuleNameStr = ModuleName.ToString();
	ModulePackageName = *FString::Printf(TEXT("/Script/%.*s"), ModuleNameStr.Len(), *ModuleNameStr);
#endif

	InternalTag = TagName.IsNone() ? FMessageTag() : FMessageTag(TagName);
#if WITH_EDITOR
	DeveloperComment = TagDevComment;
#endif

	GetRegisteredNativeTags().Add(this);

	if (UMessageTagsManager* Manager = UMessageTagsManager::GetIfAllocated())
	{
		Manager->AddNativeMessageTag(this);
	}
}

FNativeMessageTag::~FNativeMessageTag()
{
	GetRegisteredNativeTags().Remove(this);

	if (UMessageTagsManager* Manager = UMessageTagsManager::GetIfAllocated())
	{
		Manager->RemoveNativeMessageTag(this);
	}
}

FName FNativeMessageTag::NAME_NativeMessageTag("Native");

#if !UE_BUILD_SHIPPING

void FNativeMessageTag::ValidateTagRegistration() const
{
	if (bValidated)
	{
		return;
	}

	bValidated = true;

	// Running commandlets or programs won't have projects potentially, so we can't assume there's a project.
	if (const FProjectDescriptor* const CurrentProject = IProjectManager::Get().GetCurrentProject())
	{
		const FModuleDescriptor* ProjectModule =
			CurrentProject->Modules.FindByPredicate([this](const FModuleDescriptor& Module) { return Module.Name == ModuleName; });

		if (!VerifyModuleCanContainMessageTag(ModuleName, InternalTag.GetTagName(), ProjectModule, TSharedPtr<IPlugin>()))
		{
			const FModuleDescriptor* PluginModule = nullptr;

			// Ok, so we're not in a module for the project, 
			TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName.ToString());
			if (Plugin.IsValid())
			{
				const FPluginDescriptor& PluginDescriptor = Plugin->GetDescriptor();
				PluginModule = PluginDescriptor.Modules.FindByPredicate([this](const FModuleDescriptor& Module) { return Module.Name == ModuleName; });
			}

			if (!VerifyModuleCanContainMessageTag(ModuleName, InternalTag.GetTagName(), PluginModule, Plugin))
			{
				ensureAlwaysMsgf(false, TEXT("Unable to find information about module '%s' in plugin '%s'"), *ModuleName.ToString(), *PluginName.ToString());
			}
		}
	}
}

#endif

TSet<const FNativeMessageTag*>& FNativeMessageTag::GetRegisteredNativeTags()
{
	static TSet<const class FNativeMessageTag*> RegisteredNativeTags;
	return RegisteredNativeTags;
}

#undef LOCTEXT_NAMESPACE
#endif