using UnrealBuildTool;

// Testbed-only editor module: smoke test fixtures and test hooks exposed as the "testbed" toolset. Not part of the AgentMcp plugin.
public class AgentMcpTestbedEditor : ModuleRules
{
	public AgentMcpTestbedEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"AssetTools",
			"Slate",
			"SlateCore",
			"UMG",
			"UMGEditor",
			"AgentMcpToolset",
			"AgentMcpTestbed",
		});
	}
}
