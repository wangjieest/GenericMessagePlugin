// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MessageTags : ModuleRules
	{
		public MessageTags(ReadOnlyTargetRules Target)
			: base(Target)
		{
			PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
			PublicIncludePaths.AddRange(new string[]{
				ModuleDirectory + "/Public",
				ModuleDirectory + "/Classes",
			});

			PrivateIncludePaths.AddRange(new string[]{
				ModuleDirectory + "/Private",
			});

			PublicDependencyModuleNames.AddRange(new string[]{
				"Core",
				"CoreUObject",
				"Engine",
				"DeveloperSettings",
				"GMP",
			});
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Projects",
					"NetCore",
					"Json",
					"JsonUtilities",
					"DeveloperSettings",
					"Projects",
				}
			);

			if (Target.Type == TargetType.Editor)
			{
				PrivateDependencyModuleNames.AddRange(new string[]{
					"SlateCore",
					"Slate",
				});
				BuildVersion Version;
				if (BuildVersion.TryRead(BuildVersion.GetDefaultFileName(), out Version))
				{
					if (Version.MajorVersion > 4 || (Version.MajorVersion == 4 && Version.MinorVersion >= 26))
					{
						PrivateDependencyModuleNames.AddRange(new string[]{
							"DeveloperSettings"
						});
					}
				}
			}
			SetupIrisSupport(Target);
		}
	}
}
