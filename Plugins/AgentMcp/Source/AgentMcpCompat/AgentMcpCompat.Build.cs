using UnrealBuildTool;

// Compatibility layer: the only module allowed to branch on the engine version.
public class AgentMcpCompat : ModuleRules
{
	public AgentMcpCompat(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Json",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Engine",
		});
	}
}
