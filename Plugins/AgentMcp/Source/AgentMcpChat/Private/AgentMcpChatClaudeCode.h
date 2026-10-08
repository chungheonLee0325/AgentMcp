#pragma once

#include "CoreMinimal.h"

namespace UE::AgentMcp::Chat
{
	class FMcpClient;

	/** MCP server name in the configuration given to Claude Code; its tools are called mcp__<name>__<tool>. */
	extern const TCHAR* const ClaudeCodeServerName;

	/** A value of a picker with the label people see; an empty Id means the default. */
	struct FChatChoice
	{
		FString Id;
		FString Label;
	};

	/** A model a CLI lists, with the reasoning levels it takes. */
	struct FChatModel
	{
		/** The id passed to the CLI. */
		FString Id;
		FString Label;
		/** low, medium, high, xhigh, max as available; empty when the model has no level choice. */
		TArray<FString> Levels;
	};

	/** The label of a reasoning level, such as 보통 for medium. */
	FString GetEffortLabel(const FString& Level);

	/** The models offered for Claude Code, by full name so the version shows. */
	const TArray<FChatChoice>& GetClaudeCodeModelChoices();

	/** The --effort levels. */
	const TArray<FChatChoice>& GetClaudeCodeEffortChoices();

	/** The label of Id among Choices, or Id itself when it is not one of them. */
	FString GetChoiceLabel(const TArray<FChatChoice>& Choices, const FString& Id);

	struct FClaudeCodeLaunch
	{
		FString Executable;
		FString Arguments;
		FString WorkingDirectory;
	};

	/**
	 * Builds one claude -p turn. Claude Code gets no built-in tools (no files, no shell), only this editor's MCP server, and only the
	 * tools the permission mode allows; the others are denied by name. The server still asks in the panel before any change.
	 * An empty ResumeSessionId starts a new conversation.
	 */
	bool PrepareClaudeCodeLaunch(const FString& Question, const FString& SystemPrompt, const FString& ResumeSessionId, const FMcpClient& Mcp,
		FClaudeCodeLaunch& Out, FString& OutError);

	/** Tool name without the mcp__<server>__ prefix. */
	FString GetClaudeCodeToolDisplayName(const FString& ToolName);

	/** The configured claude.exe, or the one in %USERPROFILE%\.local\bin or on PATH. Empty with OutError set when none is found. */
	FString FindClaudeCodeExecutable(FString& OutError);

	/** The folder Claude Code runs in for the chat. Created when missing. */
	FString GetClaudeCodeWorkingDirectory();

	/** Command line that runs the official native installer, which puts claude.exe in %USERPROFILE%\.local\bin. */
	bool GetClaudeCodeInstallCommand(FString& OutExecutable, FString& OutArguments);

	/** Removes terminal color codes from a line of CLI output. */
	FString StripTerminalCodes(const FString& Line);
}
