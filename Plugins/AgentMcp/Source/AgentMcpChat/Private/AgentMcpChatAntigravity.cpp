#include "AgentMcpChatAntigravity.h"

#include "AgentMcpChatCliProcess.h"
#include "AgentMcpChatClaudeCode.h"
#include "AgentMcpChatMcpClient.h"
#include "AgentMcpChatPermissions.h"
#include "AgentMcpChatSettings.h"
#include "AgentMcpCompat.h"
#include "AgentMcpSettings.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UE::AgentMcp::Chat
{
	const TCHAR* const AntigravityServerName = TEXT("agentmcp-chat");

	namespace AntigravityPrivate
	{
#if PLATFORM_WINDOWS
		const TCHAR* const ExecutableName = TEXT("agy.exe");
		const TCHAR* const PathSeparator = TEXT(";");
#else
		const TCHAR* const ExecutableName = TEXT("agy");
		const TCHAR* const PathSeparator = TEXT(":");
#endif

		FString GetAllowRule()
		{
			return FString::Printf(TEXT("mcp(%s/*)"), AntigravityServerName);
		}
	}

	FString FindAntigravityExecutable(FString& OutError)
	{
		using namespace AntigravityPrivate;

		const FString Configured = GetDefault<UAgentMcpChatSettings>()->AntigravityPath.TrimStartAndEnd().TrimQuotes();
		if (!Configured.IsEmpty())
		{
			if (FPaths::FileExists(Configured))
			{
				return Configured;
			}
			OutError = FString::Printf(TEXT("설정한 Antigravity 경로에 파일이 없습니다: %s"), *Configured);
			return FString();
		}

		TArray<FString> Candidates;
		const FString LocalAppData = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
		if (!LocalAppData.IsEmpty())
		{
			Candidates.Add(FPaths::Combine(LocalAppData, TEXT("agy"), TEXT("bin"), ExecutableName));
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
		OutError = TEXT("Antigravity CLI가 설치되어 있지 않습니다. 패널의 '계정'에서 Gemini의 '설치 후 로그인'을 누르세요.");
		return FString();
	}

	bool GetAntigravityInstallCommand(FString& OutExecutable, FString& OutArguments)
	{
#if PLATFORM_WINDOWS
		OutExecutable = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot")), TEXT("System32"), TEXT("WindowsPowerShell"),
			TEXT("v1.0"), TEXT("powershell.exe"));
		OutArguments = TEXT("-NoProfile -ExecutionPolicy Bypass -Command \"irm https://antigravity.google/cli/install.ps1 | iex\"");
		return FPaths::FileExists(OutExecutable);
#else
		return false;
#endif
	}

	FString GetAntigravityWorkingDirectory()
	{
		const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AgentMcpChat"), TEXT("Antigravity")));
		IFileManager::Get().MakeDirectory(*FPaths::Combine(Directory, TEXT(".agents")), /*Tree=*/true);
		return Directory;
	}

	bool EnsureAntigravityPermission(FString& OutError)
	{
		const FString Home = FPlatformMisc::GetEnvironmentVariable(PLATFORM_WINDOWS ? TEXT("USERPROFILE") : TEXT("HOME"));
		if (Home.IsEmpty())
		{
			OutError = TEXT("사용자 폴더를 찾지 못해 Antigravity 권한을 설정하지 못했습니다.");
			return false;
		}
		const FString SettingsPath = FPaths::Combine(Home, TEXT(".gemini"), TEXT("antigravity-cli"), TEXT("settings.json"));

		TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
		FString Existing;
		if (FFileHelper::LoadFileToString(Existing, *SettingsPath) && !Existing.TrimStartAndEnd().IsEmpty())
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Existing);
			if (!FJsonSerializer::Deserialize(Reader, Settings) || !Settings.IsValid())
			{
				// A file the user edited by hand is never overwritten.
				OutError = FString::Printf(TEXT("%s을(를) 읽지 못해 권한을 추가하지 않았습니다. 파일의 JSON 형식을 확인하세요."), *SettingsPath);
				return false;
			}
		}

		TSharedPtr<FJsonObject> Permissions;
		const TSharedPtr<FJsonObject>* ExistingPermissions = nullptr;
		if (Settings->TryGetObjectField(TEXT("permissions"), ExistingPermissions))
		{
			Permissions = *ExistingPermissions;
		}
		else
		{
			Permissions = MakeShared<FJsonObject>();
			Settings->SetObjectField(TEXT("permissions"), Permissions);
		}

		TArray<TSharedPtr<FJsonValue>> Allow;
		const TArray<TSharedPtr<FJsonValue>>* ExistingAllow = nullptr;
		if (Permissions->TryGetArrayField(TEXT("allow"), ExistingAllow))
		{
			Allow = *ExistingAllow;
		}
		const FString Rule = AntigravityPrivate::GetAllowRule();
		for (const TSharedPtr<FJsonValue>& Value : Allow)
		{
			FString Text;
			if (Value.IsValid() && Value->TryGetString(Text) && Text == Rule)
			{
				return true;
			}
		}
		Allow.Add(MakeShared<FJsonValueString>(Rule));
		Permissions->SetArrayField(TEXT("allow"), Allow);

		FString Output;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
		FJsonSerializer::Serialize(Settings.ToSharedRef(), Writer);
		if (!FFileHelper::SaveStringToFile(Output, *SettingsPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("%s에 권한을 쓰지 못했습니다."), *SettingsPath);
			return false;
		}
		return true;
	}

	bool PrepareAntigravityLaunch(const FString& Prompt, const FString& ConversationId, const FMcpClient& Mcp, FAntigravityLaunch& Out,
		FString& OutError)
	{
		Out.Executable = FindAntigravityExecutable(OutError);
		if (Out.Executable.IsEmpty() || !EnsureAntigravityPermission(OutError))
		{
			return false;
		}
		Out.WorkingDirectory = GetAntigravityWorkingDirectory();

		TSharedRef<FJsonObject> Server = MakeShared<FJsonObject>();
		Server->SetStringField(TEXT("serverUrl"), Mcp.GetEndpoint());
		// The client tag makes the server ask in the panel before any change.
		TSharedRef<FJsonObject> Headers = MakeShared<FJsonObject>();
		Headers->SetStringField(TEXT("X-AgentMcp-Client"), ChatClientTag);
		const FString& AuthToken = GetDefault<UAgentMcpSettings>()->AuthToken;
		if (!AuthToken.IsEmpty())
		{
			Headers->SetStringField(TEXT("Authorization"), TEXT("Bearer ") + AuthToken);
		}
		Server->SetObjectField(TEXT("headers"), Headers);
		// Tools outside the permission mode are hidden from the model, so it cannot even try them.
		TArray<TSharedPtr<FJsonValue>> Disabled;
		for (const FString& Name : Mcp.GetBlockedToolNames())
		{
			Disabled.Add(MakeShared<FJsonValueString>(Name));
		}
		Server->SetArrayField(TEXT("disabledTools"), Disabled);
		TSharedRef<FJsonObject> Servers = MakeShared<FJsonObject>();
		Servers->SetObjectField(AntigravityServerName, Server);
		TSharedRef<FJsonObject> Config = MakeShared<FJsonObject>();
		Config->SetObjectField(TEXT("mcpServers"), Servers);

		const FString ConfigPath = FPaths::Combine(Out.WorkingDirectory, TEXT(".agents"), TEXT("mcp_config.json"));
		if (!FFileHelper::SaveStringToFile(UE::AgentMcp::Compat::JsonObjectToString(Config), *ConfigPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("MCP 설정 파일을 쓰지 못했습니다: %s"), *ConfigPath);
			return false;
		}

		const UAgentMcpChatSettings* Settings = GetDefault<UAgentMcpChatSettings>();
		TArray<FString> Arguments;
		Arguments.Add(TEXT("-p"));
		Arguments.Add(QuoteCommandLineArgument(Prompt));
		Arguments.Add(TEXT("--output-format stream-json"));
		Arguments.Add(TEXT("--disable-slash-commands"));
		FString Model = Settings->AntigravityModel.TrimStartAndEnd();
		FString Effort = Settings->AntigravityEffort.TrimStartAndEnd();
		// Gemini takes only these levels; an older saved xhigh or max would fail the whole request.
		if (Effort != TEXT("low") && Effort != TEXT("medium") && Effort != TEXT("high"))
		{
			Effort.Reset();
		}
		if (!Effort.IsEmpty())
		{
			// agy lists each Gemini model once per level (gemini-3.8-flash-medium); such an id refuses --effort, while the base name
			// takes it.
			Model = GetAntigravityBaseModel(Model);
		}
		if (!Model.IsEmpty())
		{
			Arguments.Add(TEXT("--model ") + QuoteCommandLineArgument(Model));
		}
		if (!Effort.IsEmpty())
		{
			Arguments.Add(TEXT("--effort ") + QuoteCommandLineArgument(Effort));
		}
		if (!ConversationId.IsEmpty())
		{
			Arguments.Add(TEXT("--conversation ") + QuoteCommandLineArgument(ConversationId));
		}
		Out.Arguments = FString::Join(Arguments, TEXT(" "));
		return true;
	}

	const TArray<FChatChoice>& GetAntigravityEffortChoices()
	{
		static const TArray<FChatChoice> Choices = {
			{ FString(), TEXT("추론: 모델 이름대로") },
			{ TEXT("low"), TEXT("추론: 낮음") },
			{ TEXT("medium"), TEXT("추론: 보통") },
			{ TEXT("high"), TEXT("추론: 높음") },
		};
		return Choices;
	}

	FString GetAntigravityBaseModel(const FString& Id)
	{
		FString Base = Id;
		for (const TCHAR* Suffix : { TEXT("-low"), TEXT("-medium"), TEXT("-high") })
		{
			if (Base.RemoveFromEnd(Suffix))
			{
				break;
			}
		}
		return Base;
	}

	TArray<FChatModel> ParseAntigravityModels(const FString& Output)
	{
		TArray<FChatModel> Models;
		TArray<FString> Lines;
		Output.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			FString Id;
			FString Label;
			if (!Line.Split(TEXT("\t"), &Id, &Label))
			{
				continue;
			}
			Id.TrimStartAndEndInline();
			Label.TrimStartAndEndInline();
			if (Id.IsEmpty() || Id.Contains(TEXT(" ")))
			{
				continue;
			}

			const FString Base = GetAntigravityBaseModel(Id);
			const FString Level = Base.Len() < Id.Len() ? Id.RightChop(Base.Len() + 1) : FString();
			// "Gemini 3.8 Flash (High)" -> "Gemini 3.8 Flash"; other parentheses, such as "(Thinking)", stay.
			FString BaseLabel = Label.IsEmpty() ? Id : Label;
			if (!Level.IsEmpty())
			{
				int32 Open = INDEX_NONE;
				if (BaseLabel.FindLastChar(TEXT('('), Open) && BaseLabel.Mid(Open + 1).StartsWith(Level, ESearchCase::IgnoreCase))
				{
					BaseLabel = BaseLabel.Left(Open).TrimEnd();
				}
			}

			FChatModel* Existing = Models.FindByPredicate([&Base](const FChatModel& Model)
			{
				return GetAntigravityBaseModel(Model.Id) == Base;
			});
			if (!Existing)
			{
				FChatModel& Model = Models.AddDefaulted_GetRef();
				// Until a second level shows up, the listed id is the one to pass.
				Model.Id = Id;
				Model.Label = BaseLabel;
				if (!Level.IsEmpty())
				{
					Model.Levels.Add(Level);
				}
				continue;
			}
			Existing->Id = Base;
			Existing->Label = BaseLabel;
			if (!Level.IsEmpty())
			{
				Existing->Levels.AddUnique(Level);
			}
		}

		// One level is no choice: the listed id is passed as it is.
		for (FChatModel& Model : Models)
		{
			if (Model.Levels.Num() < 2)
			{
				Model.Levels.Reset();
			}
			Model.Levels.Sort([](const FString& A, const FString& B)
			{
				auto Rank = [](const FString& Level) { return Level == TEXT("low") ? 0 : Level == TEXT("medium") ? 1 : 2; };
				return Rank(A) < Rank(B);
			});
		}
		return Models;
	}
}
