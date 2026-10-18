using UnrealBuildTool;

/// <summary>Synthetic GPU checks shared by the editor and Development game builds; excluded from Shipping.</summary>
public class WeltallRenderingTests : ModuleRules
{
	public WeltallRenderingTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Each test keeps file-local helpers with names also used by other tests.
		bUseUnity = false;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "Weltall", "WeltallCore", "RenderCore", "RHI" });
		PrivateDependencyModuleNames.AddRange(new[] { "CoreUObject", "Engine", "ImageWrapper" });
	}
}
