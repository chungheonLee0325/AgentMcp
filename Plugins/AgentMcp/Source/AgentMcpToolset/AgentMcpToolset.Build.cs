using UnrealBuildTool;

// Tool layer: toolset discovery, JSON schema generation, reflection invocation,
// dispatcher policies (game thread, busy wait, transactions, PIE guard) and server bootstrap.
public class AgentMcpToolset : ModuleRules
{
	public AgentMcpToolset(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
			// Public headers (AgentMcpJson.h, AgentMcpAsyncResult.h) expose FJsonObject.
			"Json",
		});

		// AgentMcpProtocol stays private so toolset implementations (AgentMcpTools) cannot reach MCP internals.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"JsonUtilities",
			"UnrealEd",
			// Skills are found in the plugin folder.
			"Projects",
			"AgentMcpCompat",
			"AgentMcpProtocol",
		});
	}
}
