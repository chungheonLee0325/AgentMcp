#pragma once

#include "CoreMinimal.h"

/**
 * Writes the client configuration files that tell an agent where this editor serves MCP, so a fresh clone only needs a build.
 * The project files are written when the server starts; the file in the user folder is only written when asked.
 */
namespace UE::AgentMcp::ClientConfig
{
	struct FWriteResult
	{
		/** Files that were created or changed. */
		TArray<FString> Written;

		/** Files that already named this endpoint. */
		TArray<FString> Unchanged;

		/** "<file>: <reason>" for each file that could not be written. */
		TArray<FString> Failures;
	};

	/**
	 * <Project>/.mcp.json for Claude Code and <Project>/.codex/config.toml for Codex. While an AuthToken is set they are left alone and
	 * reported as failures, since project files are often committed and would carry the token.
	 */
	AGENTMCPTOOLSET_API void WriteProjectFiles(const FString& EndpointUrl, const FString& AuthToken, const FString& ServerName, FWriteResult& Out);

	/**
	 * Points the <ServerName> entry of the user's ~/.codex/config.toml at this editor and leaves the rest of the file alone.
	 * Codex reads a project file only in a trusted folder, so this is what reaches every folder, including new worktrees.
	 */
	AGENTMCPTOOLSET_API void WriteUserCodexConfig(const FString& EndpointUrl, const FString& AuthToken, const FString& ServerName, FWriteResult& Out);
}
