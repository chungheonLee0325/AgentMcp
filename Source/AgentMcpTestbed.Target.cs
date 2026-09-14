using UnrealBuildTool;

public class AgentMcpTestbedTarget : TargetRules
{
	public AgentMcpTestbedTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
		ExtraModuleNames.Add("AgentMcpTestbed");
	}
}
