using UnrealBuildTool;

public class AgentMcpTestbed : ModuleRules
{
	public AgentMcpTestbed(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			// The UI sample theme is chosen in the project settings.
			"DeveloperSettings",
			"UMG",
			"Slate",
			"SlateCore",
		});
	}
}
