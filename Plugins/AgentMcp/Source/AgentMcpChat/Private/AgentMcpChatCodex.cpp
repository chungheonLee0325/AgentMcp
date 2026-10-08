#include "AgentMcpChatCodex.h"

#include "AgentMcpChatCliProcess.h"
#include "AgentMcpChatMcpClient.h"
#include "AgentMcpChatPermissions.h"
#include "AgentMcpChatSettings.h"
#include "AgentMcpSettings.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UE::AgentMcp::Chat
{
	const TCHAR* const CodexServerName = TEXT("agentmcp-chat");

	namespace CodexPrivate
	{
#if PLATFORM_WINDOWS
		const TCHAR* const ExecutableName = TEXT("codex.exe");
		const TCHAR* const PathSeparator = TEXT(";");
#else
		const TCHAR* const ExecutableName = TEXT("codex");
		const TCHAR* const PathSeparator = TEXT(":");
#endif

		/**
		 * Codex features that give the model tools of its own: shell, browser, computer use, apps, plugins, sub-agents and so on. All
		 * are switched off, so the editor's MCP tools are the only way the model touches anything.
		 */
		const TCHAR* const DisabledFeatures[] = {
			TEXT("shell_tool"), TEXT("unified_exec"), TEXT("shell_snapshot"), TEXT("apps"), TEXT("browser_use"), TEXT("browser_use_external"),
			TEXT("in_app_browser"), TEXT("computer_use"), TEXT("image_generation"), TEXT("multi_agent"), TEXT("plugins"), TEXT("remote_plugin"),
			TEXT("hooks"), TEXT("skill_search"), TEXT("tool_suggest"), TEXT("view_image"), TEXT("goals"),
		};

		/** One -c key=value override, quoted as a single argument so the TOML quotes inside it survive. */
		FString Override(const FString& KeyValue)
		{
			return TEXT("-c ") + QuoteCommandLineArgument(KeyValue);
		}

		FString TomlString(const FString& Value)
		{
			return TEXT("\"") + Value.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\"")) + TEXT("\"");
		}
	}

	FString FindCodexExecutable(FString& OutError)
	{
		using namespace CodexPrivate;

		const FString Configured = GetDefault<UAgentMcpChatSettings>()->CodexPath.TrimStartAndEnd().TrimQuotes();
		if (!Configured.IsEmpty())
		{
			if (FPaths::FileExists(Configured))
			{
				return Configured;
			}
			OutError = FString::Printf(TEXT("설정한 Codex 경로에 파일이 없습니다: %s"), *Configured);
			return FString();
		}

		TArray<FString> Candidates;
		const FString LocalAppData = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
		if (!LocalAppData.IsEmpty())
		{
			Candidates.Add(FPaths::Combine(LocalAppData, TEXT("Programs"), TEXT("OpenAI"), TEXT("Codex"), TEXT("bin"), ExecutableName));
		}
		TArray<FString> PathEntries;
		FPlatformMisc::GetEnvironmentVariable(TEXT("PATH")).ParseIntoArray(PathEntries, PathSeparator, /*InCullEmpty=*/true);
		for (const FString& Entry : PathEntries)
		{
			// The Microsoft Store app's codex.exe under WindowsApps cannot be started from outside its package.
			if (!Entry.Contains(TEXT("WindowsApps")))
			{
				Candidates.Add(FPaths::Combine(Entry.TrimQuotes(), ExecutableName));
			}
		}
		for (const FString& Candidate : Candidates)
		{
			if (FPaths::FileExists(Candidate))
			{
				return Candidate;
			}
		}
		OutError = TEXT("Codex CLI가 설치되어 있지 않습니다. 패널의 '계정'에서 ChatGPT의 '설치 후 로그인'을 누르세요.");
		return FString();
	}

	bool GetCodexInstallCommand(FString& OutExecutable, FString& OutArguments)
	{
#if PLATFORM_WINDOWS
		OutExecutable = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot")), TEXT("System32"), TEXT("WindowsPowerShell"),
			TEXT("v1.0"), TEXT("powershell.exe"));
		OutArguments = TEXT("-NoProfile -ExecutionPolicy Bypass -Command \"irm https://chatgpt.com/codex/install.ps1 | iex\"");
		return FPaths::FileExists(OutExecutable);
#else
		return false;
#endif
	}

	FString GetCodexWorkingDirectory()
	{
		const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AgentMcpChat"), TEXT("Codex")));
		IFileManager::Get().MakeDirectory(*Directory, /*Tree=*/true);
		return Directory;
	}

	bool PrepareCodexLaunch(const FString& Prompt, const FString& ThreadId, const FMcpClient& Mcp, FCodexLaunch& Out, FString& OutError)
	{
		using namespace CodexPrivate;

		Out.Executable = FindCodexExecutable(OutError);
		if (Out.Executable.IsEmpty())
		{
			return false;
		}
		Out.WorkingDirectory = GetCodexWorkingDirectory();

		TArray<FString> Arguments;
		Arguments.Add(TEXT("exec"));
		if (!ThreadId.IsEmpty())
		{
			Arguments.Add(TEXT("resume"));
		}
		Arguments.Add(TEXT("--json"));
		// The user's config.toml (their plugins, MCP servers and model settings) stays out of the chat; the login still comes from ~/.codex.
		Arguments.Add(TEXT("--ignore-user-config"));
		Arguments.Add(TEXT("--ignore-rules"));
		Arguments.Add(TEXT("--skip-git-repo-check"));
		// exec resume has no -s, so the sandbox is set the same way for both.
		Arguments.Add(Override(TEXT("sandbox_mode=\"read-only\"")));
		Arguments.Add(Override(TEXT("web_search=\"disabled\"")));
		for (const TCHAR* Feature : DisabledFeatures)
		{
			Arguments.Add(FString::Printf(TEXT("--disable %s"), Feature));
		}

		const FString Prefix = FString::Printf(TEXT("mcp_servers.%s."), CodexServerName);
		Arguments.Add(Override(Prefix + TEXT("url=") + TomlString(Mcp.GetEndpoint())));
		// The client tag makes the server ask in the panel before any change, so Codex itself need not ask.
		Arguments.Add(Override(Prefix + TEXT("http_headers.X-AgentMcp-Client=") + TomlString(ChatClientTag)));
		const FString& AuthToken = GetDefault<UAgentMcpSettings>()->AuthToken;
		if (!AuthToken.IsEmpty())
		{
			Arguments.Add(Override(Prefix + TEXT("http_headers.Authorization=") + TomlString(TEXT("Bearer ") + AuthToken)));
		}
		Arguments.Add(Override(Prefix + TEXT("default_tools_approval_mode=\"approve\"")));
		if (Mcp.GetBlockedToolNames().Num() > 0)
		{
			TArray<FString> Quoted;
			for (const FString& Name : Mcp.GetBlockedToolNames())
			{
				Quoted.Add(TomlString(Name));
			}
			Arguments.Add(Override(Prefix + TEXT("disabled_tools=[") + FString::Join(Quoted, TEXT(",")) + TEXT("]")));
		}

		const UAgentMcpChatSettings* Settings = GetDefault<UAgentMcpChatSettings>();
		const FString Model = Settings->CodexModel.TrimStartAndEnd();
		if (!Model.IsEmpty())
		{
			Arguments.Add(TEXT("-m ") + QuoteCommandLineArgument(Model));
		}
		const FString Effort = Settings->CodexEffort.TrimStartAndEnd();
		if (!Effort.IsEmpty())
		{
			Arguments.Add(Override(TEXT("model_reasoning_effort=") + TomlString(Effort)));
		}

		if (!ThreadId.IsEmpty())
		{
			Arguments.Add(QuoteCommandLineArgument(ThreadId));
		}
		Arguments.Add(QuoteCommandLineArgument(Prompt));
		Out.Arguments = FString::Join(Arguments, TEXT(" "));
		return true;
	}

	TArray<FChatModel> ParseCodexModels(const FString& Output)
	{
		TArray<FChatModel> Models;
		TSharedPtr<FJsonObject> Catalog;
		const int32 Start = Output.Find(TEXT("{"));
		if (Start == INDEX_NONE)
		{
			return Models;
		}
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Output.RightChop(Start));
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (!FJsonSerializer::Deserialize(Reader, Catalog) || !Catalog.IsValid() || !Catalog->TryGetArrayField(TEXT("models"), Entries))
		{
			return Models;
		}

		for (const TSharedPtr<FJsonValue>& Value : *Entries)
		{
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			FString Slug;
			FString Visibility;
			if (!Value->TryGetObject(Entry) || !(*Entry)->TryGetStringField(TEXT("slug"), Slug)
				|| !(*Entry)->TryGetStringField(TEXT("visibility"), Visibility) || Visibility != TEXT("list"))
			{
				continue;
			}
			FChatModel& Model = Models.AddDefaulted_GetRef();
			Model.Id = Slug;
			if (!(*Entry)->TryGetStringField(TEXT("display_name"), Model.Label))
			{
				Model.Label = Slug;
			}
			const TArray<TSharedPtr<FJsonValue>>* Levels = nullptr;
			if ((*Entry)->TryGetArrayField(TEXT("supported_reasoning_levels"), Levels))
			{
				for (const TSharedPtr<FJsonValue>& LevelValue : *Levels)
				{
					const TSharedPtr<FJsonObject>* Level = nullptr;
					FString Effort;
					if (LevelValue->TryGetObject(Level) && (*Level)->TryGetStringField(TEXT("effort"), Effort) && Effort != TEXT("ultra"))
					{
						Model.Levels.Add(Effort);
					}
				}
			}
		}
		return Models;
	}
}
