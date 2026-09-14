using UnrealBuildTool;

// Unreal layer: concrete toolsets (UAgentMcpToolset subclasses) that call editor APIs.
// Deliberately does not depend on AgentMcpProtocol: tools only return values or raise tool errors.
public class AgentMcpTools : ModuleRules
{
	public AgentMcpTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"AssetRegistry",
			"Projects",
			"Json",
			"JsonUtilities",
			// pie_start plays in the active level viewport.
			"LevelEditor",
			"Slate",
			"SlateCore",
			// blueprint_inspect (Blueprint type names) and the umg tools (widget trees, BindWidget, Widget Blueprint creation).
			"BlueprintGraph",
			"UMG",
			"UMGEditor",
			"AssetTools",
			// Save warnings for packages under source control.
			"SourceControl",
			// viewport_capture encodes PNG images.
			"ImageWrapper",
			"AgentMcpToolset",
		});

		// livecoding_compile reaches ILiveCodingModule through FModuleManager, as UnrealEd.Build.cs does.
		if (Target.bWithLiveCoding)
		{
			PrivateIncludePathModuleNames.Add("LiveCoding");
		}
	}
}
