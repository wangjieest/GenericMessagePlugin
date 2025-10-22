//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using System.Text;

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
			Path.Combine(EngineDirectory, "Source/Runtime/Online/HTTP/Public"),
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
			PrivateDefinitions.Add("GMP_DEBUGGAME=1");
			if (Target.Type == TargetType.Editor)
				PrivateDefinitions.Add("GMP_DEBUGGAME_EDITOR=1");
			else
				PrivateDefinitions.Add("GMP_DEBUGGAME_EDITOR=0");
		}
		else
		{
			if (!Target.bIsEngineInstalled)
			{
				// always add "GMP" as PrivateDependencyModuleNames
				SharedPCHHeaderFile = ModuleDirectory + "/Shared/GMPCore.h";
			}

			PrivateDefinitions.Add("GMP_DEBUGGAME=0");
			PrivateDefinitions.Add("GMP_DEBUGGAME_EDITOR=0");
		}
		DynamicallyLoadedModuleNames.AddRange(new string[] {
			// ... add any modules that your module loads dynamically here ...
		});

		PrivateDefinitions.Add("UPB_BUILD_API=1");
		PrivateDefinitions.Add("UPB_DESC_PREFIX=google_upb_");

		bool bEnableProtoExtensions = true;
		if (bEnableProtoExtensions)
		{
			PublicDefinitions.Add("GMP_WITH_UPB=1");

			bool bEnableProtoEditorGenerator = true;
			if (bEnableProtoEditorGenerator && Target.Type == TargetType.Editor && !Target.bIsEngineInstalled)
			{
				PrivateDefinitions.Add("GMP_WITH_PROTO_GENERATOR");
				PrivateDependencyModuleNames.AddRange(new string[] {
							"Protobuf", // compile proto to proto descriptor binary
							"Slate",    // select proto files
							"SlateCore",
						});
			}
		}

		bool bEnableYamlExtensions = false;
		if (bEnableYamlExtensions)
		{
			PrivateDefinitions.Add("GMP_WITH_YAML=1");
		}
		else
		{
			PrivateDefinitions.Add("GMP_WITH_YAML=0");
		}

		BuildVersion Version;
		if (BuildVersion.TryRead(BuildVersion.GetDefaultFileName(), out Version))
		{
			if (Version.MajorVersion > 4 || (Version.MajorVersion == 4 && Version.MinorVersion > 23))
			{
				PublicDependencyModuleNames.Add("NetCore");
			}

			if (Version.MajorVersion > 4)
			{
				PrivateDependencyModuleNames.AddRange(new string[] {
					"StructUtils",
				});
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
		
		bool bEnableAndroidUIThreadSupport = false;
		if (bEnableAndroidUIThreadSupport && Target.Platform == UnrealTargetPlatform.Android)
		{
			PrivateDefinitions.Add("GMP_WITH_ANDROID_UI_THREAD=1");
            PrivateDependencyModuleNames.Add("Launch");
            string GenDir = Path.Combine(ModuleDirectory, "Intermediate", "Android");
            Directory.CreateDirectory(GenDir);
            string UplPath = Path.Combine(GenDir, "Generated_Dispatch_UPL.xml");
            var xml = new StringBuilder();
            xml.AppendLine(@"<?xml version=""1.0"" encoding=""utf-8""?>");
            xml.AppendLine(@"<root xmlns:android=""http://schemas.android.com/apk/res/android"">");
            xml.AppendLine(@"  <plugins>");
            xml.AppendLine(@"    <plugin name=""DispatchUPL_Generated"" enabled=""true"">");
            xml.AppendLine(@"      <language>UPL</language>");
            xml.AppendLine(@"      <script>");

            // imports
            xml.AppendLine(@"        <gameActivityImportAdditions>");
            xml.AppendLine(@"          import android.os.Handler;");
            xml.AppendLine(@"          import android.os.Looper;");
            xml.AppendLine(@"        </gameActivityImportAdditions>");

            // class additions
            xml.AppendLine(@"        <gameActivityClassAdditions><![CDATA[");
            xml.AppendLine(@"          private static final Handler __ue_dispatch_main = new Handler(Looper.getMainLooper());");
            xml.AppendLine(@"          public static void gmpPostTFunctionToUIThread(final long ptr) {");
            xml.AppendLine(@"              __ue_dispatch_main.post(new Runnable() { @Override public void run() { gmpNativeRunNativeTFunction(ptr); } });");
            xml.AppendLine(@"          }");
            xml.AppendLine(@"          public static boolean gmpIsOnUiThread() {");
            xml.AppendLine(@"              return Thread.currentThread() == Looper.getMainLooper().getThread();");
            xml.AppendLine(@"          }");
            xml.AppendLine(@"          private static native void gmpNativeRunNativeTFunction(long ptr);");
            xml.AppendLine(@"        ]]></gameActivityClassAdditions>");

            // proguard
            xml.AppendLine(@"        <proguardAdditions>");
            xml.AppendLine(@"          -keepclassmembers class * extends android.app.Activity { public static void gmpPostTFunctionToUIThread(long); private static native void gmpNativeRunNativeTFunction(long); public static boolean gmpIsOnUiThread(); }");
            xml.AppendLine(@"        </proguardAdditions>");

            xml.AppendLine(@"      </script>");
            xml.AppendLine(@"    </plugin>");
            xml.AppendLine(@"  </plugins>");
            xml.AppendLine(@"</root>");
            File.WriteAllText(UplPath, xml.ToString(), new UTF8Encoding(encoderShouldEmitUTF8Identifier:false));
            AdditionalPropertiesForReceipt.Add("AndroidPlugin", UplPath);
        }
		else
		{
			PrivateDefinitions.Add("GMP_WITH_ANDROID_UI_THREAD=0");
		}
	}
}
