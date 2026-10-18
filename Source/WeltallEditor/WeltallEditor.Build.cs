using UnrealBuildTool;

/// <summary>Editor-only code: automation tests and headless tools that exercise the game without a window. Never shipped.</summary>
public class WeltallEditor : ModuleRules
{
	public WeltallEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Each test keeps its helpers in an anonymous namespace of its own file.
		bUseUnity = false;
		PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "Weltall", "WeltallCore", "WeltallRenderingTests", "RenderCore", "RHI", "UnrealEd", "ImageWrapper", "ApplicationCore", "InputCore" });
	}
}
