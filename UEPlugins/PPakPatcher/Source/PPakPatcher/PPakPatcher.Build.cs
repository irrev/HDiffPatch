
using UnrealBuildTool;
using System.IO;
using System;

public class PPakPatcher : ModuleRules
{
	public PPakPatcher(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;


		//PrivateIncludePaths.Add(LibPath);
		//PublicLibraryPaths.Add(LibPath);
		//PrivateIncludePaths.Add(Path.Combine(LibPath, "libHDiffPatch"));
		//PrivateIncludePaths.Add(Path.Combine(LibPath, "libHDiffPatch", "HDiff"));
		//PrivateIncludePaths.Add(Path.Combine(LibPath, "libHDiffPatch", "HPatch"));
		//PrivateIncludePaths.Add(Path.Combine(LibPath, "libHDiffPatch"));
		//PublicLibraryPaths.Add(Path.Combine(LibPath, "libHDiffPatch"));

		PublicIncludePaths.AddRange(
				new string[] {
					// ... add public include paths required here ...
				}
				);

		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				// ... add other public dependencies that you statically link with here ...
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"AssetRegistry",
				//"Puerts",
				//"JsEnv",
				"Json",
				"PakFile",
				"RSA",
				"PakFileUtilities",
				// ... add private dependencies that you statically link with here ...
			}
			);
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					//"Slate",
					//"SlateCore",
					//"WorkspaceMenuStructure",
					//"InputCore",
					"UnrealEd",
					//"EditorStyle",
					//"ContentBrowser",
					//"EditorSubsystem",
					//"AssetManagerEditor",
					//"PropertyEditor",
					"SourceControl",
				}
			);
		}


		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
		PublicDefinitions.Add("_HPATCH_IS_USED_errno=1");

		bool bEnableHDiffPatch = true;
		PublicDefinitions.Add(String.Format("USE_HDIFFPATCH={0}", bEnableHDiffPatch ? "1" : "0"));

		// add HDiffPatch third party
		if(bEnableHDiffPatch)
		{
			bool bUseStaticLib = Target.Platform == UnrealTargetPlatform.Win64;

			PublicDefinitions.Add(String.Format("HDIFFPATCH_STATIC_LIB={0}", bUseStaticLib ? "1" : "0"));

			//Add header
			string HeaderPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "ThirdParty", "HDiffPatch", "include"));
			PublicIncludePaths.AddRange(new string[] { HeaderPath });

			string StaticPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "ThirdParty", "HDiffPatch", "lib"));
			string SharedPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "ThirdParty", "HDiffPatch", "shared"));
			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				PublicDefinitions.Add("HDIFFPATCH_PLATFORM_WINDOWS=1");

				if(bUseStaticLib)
				{
					PublicAdditionalLibraries.Add(Path.Combine(StaticPath, "windows", "x64", "HDiffPatch.lib"));
				}
				else
				{
					string DllPath = Path.Combine(SharedPath, "windows", "x64", "HDiffPatchShared.dll");
					//PublicDelayLoadDLLs.Add(DllPath);
					RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", "HDiffPatch.dll"), DllPath);
				}
			}
			else if (Target.Platform == UnrealTargetPlatform.Android)
			{
				PublicDefinitions.Add("HDIFFPATCH_PLATFORM_ANDROID=1");

				if (bUseStaticLib)
				{
					PublicAdditionalLibraries.Add(Path.Combine(StaticPath, "android", "arm64", "libHDiffPatch.a"));
					PublicAdditionalLibraries.Add(Path.Combine(StaticPath, "android", "armeabi", "libHDiffPatch.a"));
					PublicAdditionalLibraries.Add(Path.Combine(StaticPath, "android", "x86", "libHDiffPatch.a"));
					PublicAdditionalLibraries.Add(Path.Combine(StaticPath, "android", "x86_64", "libHDiffPatch.a"));
				}
				else
				{
					PublicAdditionalLibraries.Add(Path.Combine(SharedPath, "android", "arm64", "libHDiffPatch.so"));
					PublicAdditionalLibraries.Add(Path.Combine(SharedPath, "android", "armeabi", "libHDiffPatch.so"));
					PublicAdditionalLibraries.Add(Path.Combine(SharedPath, "android", "x86", "libHDiffPatch.so"));
					PublicAdditionalLibraries.Add(Path.Combine(SharedPath, "android", "x86_64", "libHDiffPatch.so"));
				}
			}
			else if (Target.Platform == UnrealTargetPlatform.Linux)
			{
				PublicDefinitions.Add("HDIFFPATCH_PLATFORM_LINUX=1");
				if(bUseStaticLib)
				{
					PublicAdditionalLibraries.Add(Path.Combine(StaticPath, "linux", "arm", "libHDiffPatch.a"));
					PublicAdditionalLibraries.Add(Path.Combine(StaticPath, "linux", "x64", "libHDiffPatch.a"));
					PublicAdditionalLibraries.Add(Path.Combine(StaticPath, "linux", "x86", "libHDiffPatch.a"));
				}
				else
				{
					PublicAdditionalLibraries.Add(Path.Combine(SharedPath, "linux", "arm", "libHDiffPatch.so"));
					PublicAdditionalLibraries.Add(Path.Combine(SharedPath, "linux", "x64", "libHDiffPatch.so"));
					PublicAdditionalLibraries.Add(Path.Combine(SharedPath, "linux", "x86", "libHDiffPatch.so"));
				}
			}
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				PublicDefinitions.Add("HDIFFPATCH_PLATFORM_MACOS=1");

				if (bUseStaticLib)
				{
					PublicAdditionalLibraries.Add(Path.Combine(StaticPath, "macos", "libHDiffPatch.a"));
				}
				else
				{
					PublicAdditionalLibraries.Add(Path.Combine(SharedPath, "macos", "libHDiffPatch.dylib"));
				}
			}
			else if (Target.Platform == UnrealTargetPlatform.IOS)
			{
				PublicDefinitions.Add("HDIFFPATCH_PLATFORM_IOS=1");
				if (bUseStaticLib)
				{
					PublicAdditionalLibraries.Add(Path.Combine(StaticPath, "ios", "libHDiffPatch.a"));
				}
				else
				{
					PublicAdditionalLibraries.Add(Path.Combine(SharedPath, "ios", "libHDiffPatch.dylib"));
				}
			}
		}
	}
}
