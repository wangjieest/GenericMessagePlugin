//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class GMP : ModuleRules
{
	public GMP(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicIncludePaths.AddRange(new string[] {
			ModuleDirectory,
			ModuleDirectory + "/Shared",
			ModuleDirectory + "/ThirdParty",
			// ... add public include paths required here ...
		});

		PrivateIncludePaths.AddRange(new string[] {
			ModuleDirectory + "/Private",
			ModuleDirectory + "/GMP",
			ModuleDirectory + "/ThirdParty",
			// ... add other private include paths required here ...
			Path.Combine(EngineDirectory, "Source/Runtime/Online/HTTP/Public")
	});

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",  // UBlueprintFunctionLibrary
					   // "GenericStorages",
					   // "HTTP",
		});

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
			PrivateDependencyModuleNames.Add("BlueprintGraph");
			PrivateDependencyModuleNames.Add("DesktopPlatform");
		}
		PrivateDefinitions.Add("SUPPRESS_MONOLITHIC_HEADER_WARNINGS=1");

		bool bEnableGMPHttpRequest = true;
		if (bEnableGMPHttpRequest)
		{
			PrivateDependencyModuleNames.Add("HTTP");
			PrivateDefinitions.Add("GMP_WITH_HTTP_PACKAGE=1");
		}
		else 
		{
			PrivateDefinitions.Add("GMP_WITH_HTTP_PACKAGE=0");
		}

		if (Target.Configuration == UnrealTargetConfiguration.DebugGame || Target.Configuration == UnrealTargetConfiguration.Debug)
		{
			PublicDefinitions.Add("GMP_DEBUGGAME=1");
			if (Target.Type == TargetType.Editor)
				PublicDefinitions.Add("GMP_DEBUGGAME_EDITOR=1");
			else
				PublicDefinitions.Add("GMP_DEBUGGAME_EDITOR=0");
		}
		else
		{

			if (!Target.bIsEngineInstalled)
			{
				// always add "GMP" as PrivateDependencyModuleNames
				SharedPCHHeaderFile = ModuleDirectory + "/Shared/GMPCore.h";
			}

			PublicDefinitions.Add("GMP_DEBUGGAME=0");
			PublicDefinitions.Add("GMP_DEBUGGAME_EDITOR=0");
		}
		DynamicallyLoadedModuleNames.AddRange(new string[] {
			// ... add any modules that your module loads dynamically here ...
		});

		bool bEnableProtoExtensions = true;
		if (bEnableProtoExtensions)
		{
			PublicDefinitions.Add("GMP_WITH_UPB=1");
			PublicDefinitions.Add("UPB_PUBLIC_API=GMP_API");
			PrivateDefinitions.Add("UPB_BUILD_API=1");

			bool bEnableProtoEditorGenerator = true;
			if (bEnableProtoEditorGenerator && Target.Type == TargetType.Editor && !Target.bIsEngineInstalled)
			{
				PrivateDependencyModuleNames.AddRange(new string[] {
							"Protobuf", // compile proto to proto descriptor binary
							"Slate",    // select proto files
							"SlateCore",
						});
			}
		}

		BuildVersion Version;
		if (BuildVersion.TryRead(BuildVersion.GetDefaultFileName(), out Version))
		{
			if (Version.MajorVersion > 4 || (Version.MajorVersion == 4 && Version.MinorVersion > 23))
			{
				PublicDependencyModuleNames.Add("NetCore");
			}
			bool bUE_USE_FPROPERTY = (Version.MajorVersion > 4 || (Version.MajorVersion == 4 && Version.MinorVersion >= 25));
			string IncFile = Path.Combine(ModuleDirectory, "GMP/PropertyCompatibility.include");
			if (bUE_USE_FPROPERTY)
			{
				PublicDefinitions.Add("UE_USE_UPROPERTY=0");
				File.Delete(IncFile);
			}
			else
			{
				PublicDefinitions.Add("UE_USE_UPROPERTY=1");
				if (!File.Exists(IncFile))
					File.Copy(Path.Combine(ModuleDirectory, "..", "ThirdParty/PropertyCompatibility.include"), IncFile);
			}
			bool bEnableScriptExtensions = Version.MajorVersion >= 4;
			if (bEnableScriptExtensions)
			{
				PublicDefinitions.Add("GMP_EXTEND_CONSOLE=1");
				if (Target.Platform.IsInGroup(UnrealPlatformGroup.Desktop) || Target.Configuration != UnrealTargetConfiguration.Shipping)
				{
					PrivateDependencyModuleNames.AddRange(new string[] {
						"HTTPServer",
					});
					PrivateDefinitions.Add("GMP_HTTPSERVER=1");
				}

				if (Target.Type == TargetType.Editor)
				{
					PrivateDependencyModuleNames.AddRange(new string[] {
						"PythonScriptPlugin",
					});
				}
			}
		}
	}
}
