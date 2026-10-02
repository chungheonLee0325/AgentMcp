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
			// viewport_capture draws a frame of the level viewport before reading it.
			"RenderCore",
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
			// The anim tools build anim graphs (AnimGraph editor nodes) around runtime pose nodes (AnimGraphRuntime).
			"AnimGraph",
			"AnimGraphRuntime",
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
