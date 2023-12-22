//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MessageTagsEditor : ModuleRules
	{
		public MessageTagsEditor(ReadOnlyTargetRules Target)
			: base(Target)
		{
			PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
			PublicIncludePaths.AddRange(new string[]{
				ModuleDirectory + "/Public",
			});

			PrivateIncludePaths.AddRange(new string[]{
				ModuleDirectory + "/Private",
			});
			PublicIncludePathModuleNames.AddRange(new string[]{
				"AssetTools",
				"AssetRegistry",
			});

			PrivateDependencyModuleNames.AddRange(new string[]{
				"ApplicationCore",
				"Core",
				"CoreUObject",
				"Engine",
				"AssetTools",
				"AssetRegistry",
				"MessageTags",
				"InputCore",
				"Slate",
				"SlateCore",
				"EditorStyle",
				"BlueprintGraph",
				"KismetCompiler",
				"GraphEditor",
				"ContentBrowser",
				"MainFrame",
				"UnrealEd",
				"SourceControl",
				"KismetWidgets",
				"PropertyEditor",
				"GMP",
				"Kismet",

				"AssetManagerEditor",
			});

			PrivateIncludePathModuleNames.AddRange(new string[]{
				"Settings",
				"SettingsEditor",
				"Projects",
			});

			BuildVersion Version;
			if (BuildVersion.TryRead(BuildVersion.GetDefaultFileName(), out Version))
			{
				if (Version.MajorVersion == 4 && Version.MinorVersion < 20)
					PrivateDependencyModuleNames.Add("ReferenceViewer");
				if (Version.MajorVersion > 4 || (Version.MajorVersion == 4 && Version.MinorVersion >= 24))
					PrivateDependencyModuleNames.Add("ToolMenus");
				if (Version.MajorVersion > 4 || (Version.MajorVersion == 4 && Version.MinorVersion >= 26))
					PrivateDependencyModuleNames.Add("ContentBrowserData");
				if(Version.MajorVersion > 4)
					PrivateDependencyModuleNames.Add("EditorFramework");
				if(Version.MajorVersion >= 5  && Version.MinorVersion > 0)
					PrivateDependencyModuleNames.Add("ToolWidgets");
			}
		}
	}
}
