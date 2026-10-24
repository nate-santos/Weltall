using UnrealBuildTool;

public class WeltallTarget : TargetRules
{
	public WeltallTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("Weltall");

		if (Target.Configuration == UnrealTargetConfiguration.Shipping)
		{
			BuildEnvironment = TargetBuildEnvironment.Unique;
			// Desktop releases retain reports; Android alphas compile out runtime logging.
			bUseLoggingInShipping = Target.Platform != UnrealTargetPlatform.Android;
			if (Target.Platform == UnrealTargetPlatform.Android)
			{
				bUseConsoleInShipping = false;
				bUseExecCommandsInShipping = false;
				bEnableTrace = false;
				bForceDisableAutomationTests = true;
				GlobalDefinitions.Add("CSV_PROFILER_ENABLE_IN_SHIPPING=0");
			}
			// A single-player game without navigation: neither the engine's server code nor its navigation meshes.
			bWithServerCode = false;
			bCompileRecast = false;
		}
	}
}
