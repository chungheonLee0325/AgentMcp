using UnrealBuildTool;

// Chat layer: an editor panel where a person asks an LLM (Claude, ChatGPT, Gemini) about the project and the model calls the read-only
// tools. It reaches the tools as an MCP client of the local endpoint, the same path external agents take, so the dispatcher policies
// (game thread, busy wait, PIE guard) apply unchanged.
public class AgentMcpChat : ModuleRules
{
	public AgentMcpChat(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
			"Slate",
			"SlateCore",
			"InputCore",
			"ToolMenus",
			// The Settings button opens Editor Preferences.
			"Settings",
			"HTTP",
			"Json",
			"AgentMcpCompat",
			// UAgentMcpSettings and GetRuntimeInfo, for the endpoint and its token.
			"AgentMcpToolset",
		});
	}
}
