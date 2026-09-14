using UnrealBuildTool;

// MCP layer: transport, JSON-RPC, sessions, tool contract.
// Must not depend on editor modules (UnrealEd and so on), so that it stays independent of the tools.
public class AgentMcpProtocol : ModuleRules
{
	public AgentMcpProtocol(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"Json",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"HTTPServer",
			"AgentMcpCompat",
		});
	}
}
