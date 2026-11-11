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

		PrivateDependencyModuleNames.AddRange(new string[] { "SlateCore", "ApplicationCore" });

		// 제미니 AI봇: LLM 서버 HTTP + JSON, 게임패드 음성 녹음(AudioCapture 플러그인은 엔진 기본 활성)
		PrivateDependencyModuleNames.AddRange(new string[] { "HTTP", "Json", "AudioCaptureCore" });

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
			"Project_CX/Variant_SideScrolling/UI",
			"Project_CX/Variant_BrotherCombat",
			"Project_CX/Variant_BrotherCombat/Gameplay",
			"Project_CX/Variant_Sister",
			"Project_CX/Variant_Sister/Animation",
			"Project_CX/Variant_Sister/Hacking",
			"Project_CX/Variant_SpiderBot",
			"Project_CX/Variant_SpiderBot/AI",
			"Project_CX/Coop",
			"Project_CX/Coop/UI",
			"Project_CX/Coop/Gameplay",
			"Project_CX/Coop/Interfaces",
			"Project_CX/Core",
			"Project_CX/Jemini",
			"Project_CX/Jemini/Warden",
			"Project_CX/Title",
			"Project_CX/UI",
			"Project_CX/UI/Menu"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
