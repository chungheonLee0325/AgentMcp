#include "AgentMcpChatClaudeCode.h"

#include "AgentMcpChatCliProcess.h"
#include "AgentMcpChatMcpClient.h"
#include "AgentMcpChatPermissions.h"
#include "AgentMcpChatSettings.h"
#include "AgentMcpCompat.h"
#include "AgentMcpSettings.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace UE::AgentMcp::Chat
{
	const TCHAR* const ClaudeCodeServerName = TEXT("unreal");

	const TArray<FChatChoice>& GetClaudeCodeModelChoices()
	{
		static const TArray<FChatChoice> Choices = {
			{ FString(), TEXT("기본 (계정 설정)") },
			{ TEXT("claude-opus-5-5"), TEXT("Claude Opus 5.5") },
			{ TEXT("claude-sonnet-5-5"), TEXT("Claude Sonnet 5.5") },
			{ TEXT("claude-haiku-5-5"), TEXT("Claude Haiku 5.5") },
			{ TEXT("claude-fable-5-1"), TEXT("Claude Fable 5.1") },
		};
		return Choices;
	}

	const TArray<FChatChoice>& GetClaudeCodeEffortChoices()
	{
		static const TArray<FChatChoice> Choices = {
			{ FString(), TEXT("추론: 기본") },
			{ TEXT("low"), TEXT("추론: 낮음") },
			{ TEXT("medium"), TEXT("추론: 보통") },
			{ TEXT("high"), TEXT("추론: 높음") },
			{ TEXT("xhigh"), TEXT("추론: 매우 높음") },
			{ TEXT("max"), TEXT("추론: 최대") },
		};
		return Choices;
	}

	FString GetEffortLabel(const FString& Level)
	{
		if (Level == TEXT("low")) return TEXT("낮음");
		if (Level == TEXT("medium")) return TEXT("보통");
		if (Level == TEXT("high")) return TEXT("높음");
		if (Level == TEXT("xhigh")) return TEXT("매우 높음");
		if (Level == TEXT("max")) return TEXT("최대");
		return Level;
	}

	FString GetChoiceLabel(const TArray<FChatChoice>& Choices, const FString& Id)
	{
		for (const FChatChoice& Choice : Choices)
		{
			if (Choice.Id == Id)
			{
				return Choice.Label;
			}
		}
		return Id;
	}

	namespace ClaudeCodePrivate
	{
#if PLATFORM_WINDOWS
		const TCHAR* const ExecutableName = TEXT("claude.exe");
		const TCHAR* const PathSeparator = TEXT(";");
#else
		const TCHAR* const ExecutableName = TEXT("claude");
		const TCHAR* const PathSeparator = TEXT(":");
#endif

		FString FindExecutableImpl(FString& OutError)
		{
			const FString Configured = GetDefault<UAgentMcpChatSettings>()->ClaudeCodePath.TrimStartAndEnd().TrimQuotes();
			if (!Configured.IsEmpty())
			{
				if (FPaths::FileExists(Configured))
				{
					return Configured;
				}
				OutError = FString::Printf(TEXT("설정한 Claude Code 경로에 파일이 없습니다: %s"), *Configured);
				return FString();
			}

			TArray<FString> Candidates;
			const FString Home = FPlatformMisc::GetEnvironmentVariable(PLATFORM_WINDOWS ? TEXT("USERPROFILE") : TEXT("HOME"));
			if (!Home.IsEmpty())
			{
				// The native installer puts claude here.
				Candidates.Add(FPaths::Combine(Home, TEXT(".local"), TEXT("bin"), ExecutableName));
			}
			TArray<FString> PathEntries;
			FPlatformMisc::GetEnvironmentVariable(TEXT("PATH")).ParseIntoArray(PathEntries, PathSeparator, /*InCullEmpty=*/true);
			for (const FString& Entry : PathEntries)
			{
				Candidates.Add(FPaths::Combine(Entry.TrimQuotes(), ExecutableName));
			}

			for (const FString& Candidate : Candidates)
			{
				if (FPaths::FileExists(Candidate))
				{
					return Candidate;
				}
			}
			OutError = TEXT("Claude Code가 설치되어 있지 않습니다. 패널의 '계정'에서 Claude Code의 '설치 후 로그인'을 누르세요. ")
				TEXT("다른 위치에 설치했다면 설정에서 Claude Code 경로를 지정하세요.");
			return FString();
		}

		FString JoinToolNames(const TArray<FString>& Names)
		{
			TArray<FString> Prefixed;
			for (const FString& Name : Names)
			{
				Prefixed.Add(FString::Printf(TEXT("mcp__%s__%s"), ClaudeCodeServerName, *Name));
			}
			return FString::Join(Prefixed, TEXT(","));
		}
	}

	bool PrepareClaudeCodeLaunch(const FString& Question, const FString& SystemPrompt, const FString& ResumeSessionId, const FMcpClient& Mcp,
		FClaudeCodeLaunch& Out, FString& OutError)
	{
		using namespace ClaudeCodePrivate;

		Out.Executable = FindClaudeCodeExecutable(OutError);
		if (Out.Executable.IsEmpty())
		{
			return false;
		}

		Out.WorkingDirectory = GetClaudeCodeWorkingDirectory();

		TSharedRef<FJsonObject> Server = MakeShared<FJsonObject>();
		Server->SetStringField(TEXT("type"), TEXT("http"));
		Server->SetStringField(TEXT("url"), Mcp.GetEndpoint());
		// The client tag makes the server ask in the panel before any change.
		TSharedRef<FJsonObject> Headers = MakeShared<FJsonObject>();
		Headers->SetStringField(TEXT("X-AgentMcp-Client"), ChatClientTag);
		const FString& AuthToken = GetDefault<UAgentMcpSettings>()->AuthToken;
		if (!AuthToken.IsEmpty())
		{
			Headers->SetStringField(TEXT("Authorization"), TEXT("Bearer ") + AuthToken);
		}
		Server->SetObjectField(TEXT("headers"), Headers);
		TSharedRef<FJsonObject> Servers = MakeShared<FJsonObject>();
		Servers->SetObjectField(ClaudeCodeServerName, Server);
		TSharedRef<FJsonObject> McpConfig = MakeShared<FJsonObject>();
		McpConfig->SetObjectField(TEXT("mcpServers"), Servers);

		const FString McpConfigPath = FPaths::Combine(Out.WorkingDirectory, TEXT("mcp.json"));
		if (!FFileHelper::SaveStringToFile(UE::AgentMcp::Compat::JsonObjectToString(McpConfig), *McpConfigPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("MCP 설정 파일을 쓰지 못했습니다: %s"), *McpConfigPath);
			return false;
		}

		TArray<FString> ReadOnlyNames;
		for (const FToolSpec& Tool : Mcp.GetTools())
		{
			ReadOnlyNames.Add(Tool.Name);
		}

		const UAgentMcpChatSettings* Settings = GetDefault<UAgentMcpChatSettings>();
		TArray<FString> Arguments;
		// The question goes first: the variadic options below would take a trailing positional argument as one of their values.
		Arguments.Add(TEXT("-p"));
		Arguments.Add(QuoteCommandLineArgument(Question));
		Arguments.Add(TEXT("--output-format stream-json --verbose"));
		Arguments.Add(TEXT("--strict-mcp-config"));
		// Restricted mode ignores the user's own settings files, so an allow rule or a hook there cannot widen what the chat may do.
		Arguments.Add(TEXT("--restricted"));
		// Anything that would ask for permission is denied, since nobody can answer a prompt here.
		Arguments.Add(TEXT("--permission-prompts none"));
		Arguments.Add(TEXT("--disable-slash-commands"));
		Arguments.Add(TEXT("--system-prompt ") + QuoteCommandLineArgument(SystemPrompt));
		const FString Model = Settings->ClaudeCodeModel.TrimStartAndEnd();
		if (!Model.IsEmpty())
		{
			Arguments.Add(TEXT("--model ") + QuoteCommandLineArgument(Model));
		}
		const FString Effort = Settings->ClaudeCodeEffort.TrimStartAndEnd();
		if (!Effort.IsEmpty())
		{
			Arguments.Add(TEXT("--effort ") + QuoteCommandLineArgument(Effort));
		}
		if (!ResumeSessionId.IsEmpty())
		{
			Arguments.Add(TEXT("--resume ") + QuoteCommandLineArgument(ResumeSessionId));
		}
		Arguments.Add(TEXT("--mcp-config ") + QuoteCommandLineArgument(McpConfigPath));

		// No built-in tools at all: no files, no shell, no web. Project files are read through the server's source tools, which keep
		// to the readable folders and decode CP949.
		Arguments.Add(TEXT("--tools \"\""));
		Arguments.Add(TEXT("--allowedTools ") + QuoteCommandLineArgument(JoinToolNames(ReadOnlyNames)));
		if (Mcp.GetBlockedToolNames().Num() > 0)
		{
			Arguments.Add(TEXT("--disallowedTools ") + QuoteCommandLineArgument(JoinToolNames(Mcp.GetBlockedToolNames())));
		}

		Out.Arguments = FString::Join(Arguments, TEXT(" "));
		return true;
	}

	FString FindClaudeCodeExecutable(FString& OutError)
	{
		return ClaudeCodePrivate::FindExecutableImpl(OutError);
	}

	FString GetClaudeCodeWorkingDirectory()
	{
		// A folder of its own, so Claude Code reads no project CLAUDE.md and keeps its session files apart from coding sessions.
		const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AgentMcpChat"), TEXT("ClaudeCode")));
		IFileManager::Get().MakeDirectory(*Directory, /*Tree=*/true);
		return Directory;
	}

	bool GetClaudeCodeInstallCommand(FString& OutExecutable, FString& OutArguments)
	{
#if PLATFORM_WINDOWS
		OutExecutable = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot")), TEXT("System32"), TEXT("WindowsPowerShell"),
			TEXT("v1.0"), TEXT("powershell.exe"));
		OutArguments = TEXT("-NoProfile -ExecutionPolicy Bypass -Command \"irm https://claude.ai/install.ps1 | iex\"");
		return FPaths::FileExists(OutExecutable);
#else
		return false;
#endif
	}

	FString StripTerminalCodes(const FString& Line)
	{
		FString Result;
		Result.Reserve(Line.Len());
		for (int32 Index = 0; Index < Line.Len(); ++Index)
		{
			if (Line[Index] == TEXT('\x1b') && Index + 1 < Line.Len() && Line[Index + 1] == TEXT('['))
			{
				// CSI sequence: ESC [ parameters, ending with a letter.
				Index += 2;
				while (Index < Line.Len() && !FChar::IsAlpha(Line[Index]))
				{
					++Index;
				}
				continue;
			}
			Result.AppendChar(Line[Index]);
		}
		return Result.TrimStartAndEnd();
	}

	FString GetClaudeCodeToolDisplayName(const FString& ToolName)

	{
		const FString Prefix = FString::Printf(TEXT("mcp__%s__"), ClaudeCodeServerName);
		return ToolName.StartsWith(Prefix) ? ToolName.RightChop(Prefix.Len()) : ToolName;
	}
}
