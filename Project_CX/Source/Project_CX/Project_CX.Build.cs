// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Project_CX : ModuleRules
{
	public Project_CX(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Project_CX",
			"Project_CX/Variant_Platforming",
			"Project_CX/Variant_Platforming/Animation",
			"Project_CX/Variant_Combat",
			"Project_CX/Variant_Combat/AI",
			"Project_CX/Variant_Combat/Animation",
			"Project_CX/Variant_Combat/Gameplay",
			"Project_CX/Variant_Combat/Interfaces",
			"Project_CX/Variant_Combat/UI",
			"Project_CX/Variant_SideScrolling",
			"Project_CX/Variant_SideScrolling/AI",
			"Project_CX/Variant_SideScrolling/Gameplay",
			"Project_CX/Variant_SideScrolling/Interfaces",
			"Project_CX/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
