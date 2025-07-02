//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class GMPEditor : ModuleRules
{
	public GMPEditor(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// bEnableUndefinedIdentifierWarnings = false;

		PublicIncludePaths.AddRange(new string[] {
			ModuleDirectory + "/Public",
			// ... add public include paths required here ...
		});

		PrivateIncludePaths.AddRange(new string[] {
			ModuleDirectory + "/Private",
			// ModuleDirectory + "/ThirdParty",
			Path.Combine(ModuleDirectory, "../../", "GMP/ThirdParty"),
			// ... add other private include paths required here ...
		});

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"Engine",
			"CoreUObject",
			"MessageTags",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"GMP",
			"MessageTagsEditor",
			"SlateCore",
			"Slate",
			"EditorStyle",
			"Kismet",
			"InputCore",
			"KismetCompiler",
			"KismetWidgets",
			"RenderCore",
			"UnrealEd",
			"PropertyEditor",
			"BlueprintGraph",
			"GraphEditor",
			"AssetManagerEditor",
			"GameplayTasks",
		});
		PrivateDefinitions.Add("SUPPRESS_MONOLITHIC_HEADER_WARNINGS=1");

		DynamicallyLoadedModuleNames.AddRange(new string[] {
			// ... add any modules that your module loads dynamically here ...
		});

		BuildVersion Version;
		if (BuildVersion.TryRead(BuildVersion.GetDefaultFileName(), out Version))
		{
			if (Version.MajorVersion == 4 && Version.MinorVersion < 20)
				PrivateDependencyModuleNames.Add("ReferenceViewer");
			else if (Version.MajorVersion > 4 || (Version.MajorVersion == 4 && Version.MinorVersion >= 24))
				PrivateDependencyModuleNames.Add("ToolMenus");
		}
	}
}
