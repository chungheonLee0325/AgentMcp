using UnrealBuildTool;

public class AgentMcpTestbedEditorTarget : TargetRules
{
	public AgentMcpTestbedEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
		ExtraModuleNames.AddRange(new string[] { "AgentMcpTestbed", "AgentMcpTestbedEditor" });
	}
}
