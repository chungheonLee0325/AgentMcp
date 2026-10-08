#pragma once

#include "AgentMcpChatClaudeCode.h"

namespace UE::AgentMcp::Chat
{
	class FMcpClient;

	/**
	 * MCP server name in the configuration given to Antigravity. The allow rule written to the user's settings names it, so a name
	 * of its own keeps that rule from reaching any other server the user configures.
	 */
	extern const TCHAR* const AntigravityServerName;

	struct FAntigravityLaunch
	{
		FString Executable;
		FString Arguments;
		FString WorkingDirectory;
	};

	/** The configured agy.exe, or the one in %LOCALAPPDATA%/agy/bin or on PATH. Empty with OutError set when none is found. */
	FString FindAntigravityExecutable(FString& OutError);

	/** Command line that runs the official installer, which puts agy.exe in %LOCALAPPDATA%/agy/bin. */
	bool GetAntigravityInstallCommand(FString& OutExecutable, FString& OutArguments);

	/** The folder Antigravity runs in for the chat, holding its .agents/mcp_config.json. Created when missing. */
	FString GetAntigravityWorkingDirectory();

	/**
	 * Adds mcp(agentmcp-chat/*) to permissions.allow in ~/.gemini/antigravity-cli/settings.json, the only place Antigravity reads
	 * permission rules from. Other keys and rules are kept. Without it every MCP call is denied in print mode.
	 */
	bool EnsureAntigravityPermission(FString& OutError);

	/**
	 * Builds one agy -p turn. The MCP configuration offers this editor's server with every tool that is not read-only disabled, so the
	 * model never sees them. An empty ConversationId starts a new conversation.
	 */
	bool PrepareAntigravityLaunch(const FString& Prompt, const FString& ConversationId, const FMcpClient& Mcp, FAntigravityLaunch& Out,
		FString& OutError);

	/** The levels Gemini models take through agy: low, medium and high. */
	const TArray<FChatChoice>& GetAntigravityEffortChoices();

	/**
	 * Parses agy models output ("id<TAB>label" lines). agy lists one entry per reasoning level (gemini-3.8-flash-high, -medium, -low);
	 * those are folded into one model, with the base id, and its levels, which go to --effort.
	 */
	TArray<FChatModel> ParseAntigravityModels(const FString& Output);

	/** The base id of an agy model id, without its -low, -medium or -high suffix. */
	FString GetAntigravityBaseModel(const FString& Id);
}
