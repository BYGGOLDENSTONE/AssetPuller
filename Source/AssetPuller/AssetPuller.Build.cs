using System.IO;
using UnrealBuildTool;

public class AssetPuller : ModuleRules
{
	public AssetPuller(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"InputCore",
			"EditorFramework",
			"UnrealEd",
			"ToolMenus",
			"AssetRegistry",
			"DeveloperSettings",
			"Projects",
			"DesktopPlatform",
		});

		// FPackageReader (reads .uasset headers without loading them) lives in the AssetRegistry
		// module's Internal folder, which UBT only exposes to engine modules. Its methods are all
		// ASSETREGISTRY_API-exported, so including it directly compiles and links fine from a
		// project plugin; UBT just logs a warning about the include path.
		PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Source/Runtime/AssetRegistry/Internal"));
	}
}
