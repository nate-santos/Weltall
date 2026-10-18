using UnrealBuildTool;

public class Weltall : ModuleRules
{
	public Weltall(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "WeltallCore", "RenderCore" });
		PrivateDependencyModuleNames.AddRange(new[] { "ApplicationCore", "AudioExtensions", "EnhancedInput", "ImageWrapper", "InputCore", "RHI", "Slate", "SlateCore" });
		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			string PluginPath = Utils.MakePathRelativeTo(ModuleDirectory, Target.RelativeEnginePath);
			AdditionalPropertiesForReceipt.Add("AndroidPlugin", System.IO.Path.Combine(PluginPath, "Android", "Weltall_UPL.xml"));
		}
	}
}
