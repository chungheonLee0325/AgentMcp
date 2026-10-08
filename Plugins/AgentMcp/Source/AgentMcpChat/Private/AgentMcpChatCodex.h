#pragma once

#include "AgentMcpChatClaudeCode.h"

namespace UE::AgentMcp::Chat
{
	class FMcpClient;

	/** MCP server name given to Codex through -c overrides. */
	extern const TCHAR* const CodexServerName;

	struct FCodexLaunch
	{
		FString Executable;
		FString Arguments;
		FString WorkingDirectory;
	};

	/** The configured codex.exe, or the one in %LOCALAPPDATA%/Programs/OpenAI/Codex/bin or on PATH. */
	FString FindCodexExecutable(FString& OutError);

	/** Command line that runs the official installer. */
	bool GetCodexInstallCommand(FString& OutExecutable, FString& OutArguments);

	/** The folder Codex runs in for the chat. Created when missing. */
	FString GetCodexWorkingDirectory();

	/**
	 * Builds one codex exec turn, or codex exec resume when ThreadId is set. The user's own config.toml is not loaded (their plugins,
	 * MCP servers and rules stay out of the chat) and is never written; this editor's server is given with -c overrides. Codex's own
	 * tools are switched off, the sandbox is read-only, and only the tools the permission mode allows are enabled.
	 */
	bool PrepareCodexLaunch(const FString& Prompt, const FString& ThreadId, const FMcpClient& Mcp, FCodexLaunch& Out, FString& OutError);

	/**
	 * Parses codex debug models: the account's model catalog, keeping the models Codex lists in its own picker, with their reasoning
	 * levels. ultra (automatic task delegation to sub-agents) is left out, since the chat runs without sub-agents.
	 */
	TArray<FChatModel> ParseCodexModels(const FString& Output);
}
