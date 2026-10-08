#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "AgentMcpSettings.generated.h"

UENUM()
enum class EAgentMcpExposureMode : uint8
{
	/** Every tool is registered as its own MCP tool. Best with clients that defer tool schemas (Claude Code). */
	Native,
	/** Only toolsets_list, toolsets_describe and tools_call are registered, for clients that cannot defer tool schemas. */
	ToolSearch,
};

UCLASS(Config = EditorPerProjectUserSettings, meta = (DisplayName = "Agent MCP"))
class AGENTMCPTOOLSET_API UAgentMcpSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Start the MCP server when the editor finishes loading. */
	UPROPERTY(Config, EditAnywhere, Category = "Server")
	bool bAutoStartServer = true;

	/**
	 * Loopback port. Every editor that enables the plugin uses this port, so give projects that run at the same time different
	 * ports. When the port is taken, the server does not start and editor_get_state is not reachable.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Server", meta = (ClampMin = "1024", ClampMax = "65535"))
	int32 Port = 18765;

	UPROPERTY(Config, EditAnywhere, Category = "Server")
	FString UrlPath = TEXT("/mcp");

	/** When set, clients must send "Authorization: Bearer <token>". */
	UPROPERTY(Config, EditAnywhere, Category = "Server")
	FString AuthToken;

	/**
	 * Write .mcp.json and .codex/config.toml into the project folder when the server starts, so a fresh clone only needs a build
	 * before an agent finds the editor. Existing entries for other servers are kept.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Client")
	bool bWriteClientConfig = true;

	/**
	 * Also point the entry of the user's ~/.codex/config.toml at this editor. Codex reads a project file only in a folder it trusts,
	 * so this is what reaches every folder, including new worktrees. The editor that started last owns the entry.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Client")
	bool bWriteUserCodexConfig = false;

	/** Name of the MCP server in the written client configuration. */
	UPROPERTY(Config, EditAnywhere, Category = "Client")
	FString ClientServerName = TEXT("unreal");

	UPROPERTY(Config, EditAnywhere, Category = "Tools")
	EAgentMcpExposureMode ExposureMode = EAgentMcpExposureMode::Native;

	/** Tool name wildcard patterns (for example "datatable_*") hidden from clients. Takes precedence over AllowedTools. */
	UPROPERTY(Config, EditAnywhere, Category = "Tools")
	TArray<FString> BlockedTools;

	/** When non-empty, only tools matching one of these wildcard patterns are exposed. */
	UPROPERTY(Config, EditAnywhere, Category = "Tools")
	TArray<FString> AllowedTools;

	/**
	 * Project folders the source tools (source_find, source_search, source_read) may read, relative to the project folder. Their text
	 * files are readable by any client of this server; nothing outside them is.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Tools")
	TArray<FString> SourceFolders = { TEXT("Source"), TEXT("Config"), TEXT("Plugins") };

	/** Allow Write and Destructive tools while a play session is running. */
	UPROPERTY(Config, EditAnywhere, Category = "Safety")
	bool bAllowWritesDuringPIE = false;

	/**
	 * Wildcard patterns ClassName.PropertyName (for example Actor.Tags or *.RootComponent) that object_set_properties refuses.
	 * Every class in the inheritance chain of the edited object is tested.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Safety")
	TArray<FString> BlockedProperties;

	/** How long a call waits for package saving, garbage collection or async loading to finish before failing with EDITOR_BUSY. */
	UPROPERTY(Config, EditAnywhere, Category = "Safety", meta = (ClampMin = "0.0"))
	float BusyWaitTimeoutSeconds = 10.0f;

	/** Result text above this size is truncated. */
	UPROPERTY(Config, EditAnywhere, Category = "Output", meta = (ClampMin = "1024"))
	int32 MaxResultBytes = 65536;

	/** Number of log lines kept in memory for log_get_recent. */
	UPROPERTY(Config, EditAnywhere, Category = "Logs", meta = (ClampMin = "100"))
	int32 LogBufferLines = 20000;

	/**
	 * More folders with skills (task guides for agents, one SKILL.md per subfolder), searched after the plugin's Skills folder and the
	 * project's AgentMcp/Skills folder. Relative paths start at the project folder. A skill replaces one with the same name found earlier.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Skills")
	TArray<FString> SkillDirectories;
};
