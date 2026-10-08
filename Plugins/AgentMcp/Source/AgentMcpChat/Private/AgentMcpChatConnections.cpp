#include "AgentMcpChatConnections.h"

#include "AgentMcpChatAntigravity.h"
#include "AgentMcpChatClaudeCode.h"
#include "AgentMcpChatCliProcess.h"
#include "AgentMcpChatCodex.h"
#include "AgentMcpChatProviders.h"

#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UE::AgentMcp::Chat
{
	FChatConnections::~FChatConnections()
	{
		OnChanged = nullptr;
		Processes.Reset();
	}

	void FChatConnections::RefreshAll()
	{
		for (EAgentMcpChatProvider Provider : { EAgentMcpChatProvider::ClaudeCode, EAgentMcpChatProvider::Codex, EAgentMcpChatProvider::Antigravity,
			EAgentMcpChatProvider::OpenAI, EAgentMcpChatProvider::Gemini })
		{
			Refresh(Provider);
		}
	}

	void FChatConnections::Refresh(EAgentMcpChatProvider Provider)
	{
		if (Provider == EAgentMcpChatProvider::OpenAI || Provider == EAgentMcpChatProvider::Gemini || Provider == EAgentMcpChatProvider::OpenAICompatible)
		{
			FProviderConfig Config;
			FString Error;
			ReadProviderConfig(Provider, Config, Error);
			Set(Provider, Config.ApiKey.IsEmpty() ? EChatConnectionState::KeyMissing : EChatConnectionState::KeyReady,
				Config.ApiKey.IsEmpty() ? FString(TEXT("API 키 없음")) : FString(TEXT("API 키 있음")));
			return;
		}

		FString Error;
		if (Provider == EAgentMcpChatProvider::ClaudeCode)
		{
			const FString Executable = FindClaudeCodeExecutable(Error);
			if (Executable.IsEmpty())
			{
				Set(Provider, EChatConnectionState::NotInstalled, TEXT("설치되어 있지 않음"));
				return;
			}
			Run(Provider, Executable, TEXT("auth status --json"), GetClaudeCodeWorkingDirectory(), 30.0f, [this, Provider](int32 ReturnCode, const FString& Output)
			{
				TSharedPtr<FJsonObject> Status;
				const int32 Start = Output.Find(TEXT("{"));
				if (Start != INDEX_NONE)
				{
					const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Output.RightChop(Start));
					FJsonSerializer::Deserialize(Reader, Status);
				}
				bool bLoggedIn = false;
				if (!Status.IsValid())
				{
					Set(Provider, EChatConnectionState::Failed, TEXT("상태를 읽지 못함"));
					return;
				}
				Status->TryGetBoolField(TEXT("loggedIn"), bLoggedIn);
				FString Account;
				if (!Status->TryGetStringField(TEXT("email"), Account))
				{
					Status->TryGetStringField(TEXT("authMethod"), Account);
				}
				Set(Provider, bLoggedIn ? EChatConnectionState::SignedIn : EChatConnectionState::SignedOut, bLoggedIn ? Account : FString(TEXT("로그인 필요")));
			});
			return;
		}

		if (Provider == EAgentMcpChatProvider::Codex)
		{
			const FString Executable = FindCodexExecutable(Error);
			if (Executable.IsEmpty())
			{
				Set(Provider, EChatConnectionState::NotInstalled, TEXT("설치되어 있지 않음"));
				return;
			}
			Run(Provider, Executable, TEXT("login status"), GetCodexWorkingDirectory(), 30.0f, [this, Provider](int32 ReturnCode, const FString& Output)
			{
				const bool bLoggedIn = ReturnCode == 0 && Output.Contains(TEXT("Logged in"));
				FString Detail = Output.TrimStartAndEnd();
				Detail.ReplaceInline(TEXT("Logged in using "), TEXT(""));
				Set(Provider, bLoggedIn ? EChatConnectionState::SignedIn : EChatConnectionState::SignedOut, bLoggedIn ? Detail.Left(60) : FString(TEXT("로그인 필요")));
			});
			return;
		}

		if (Provider == EAgentMcpChatProvider::Antigravity)
		{
			const FString Executable = FindAntigravityExecutable(Error);
			if (Executable.IsEmpty())
			{
				Set(Provider, EChatConnectionState::NotInstalled, TEXT("설치되어 있지 않음"));
				return;
			}
			// agy has no status command; listing the models works only when signed in and costs nothing.
			Run(Provider, Executable, TEXT("models"), GetAntigravityWorkingDirectory(), 20.0f, [this, Provider](int32 ReturnCode, const FString& Output)
			{
				const int32 Count = ParseAntigravityModels(Output).Num();
				Set(Provider, Count > 0 ? EChatConnectionState::SignedIn : EChatConnectionState::SignedOut,
					Count > 0 ? FString::Printf(TEXT("사용 가능 (모델 %d개)"), Count) : FString(TEXT("로그인 필요")));
			});
		}
	}

	bool FChatConnections::CanSignOut(EAgentMcpChatProvider Provider)
	{
		return Provider == EAgentMcpChatProvider::ClaudeCode || Provider == EAgentMcpChatProvider::Codex;
	}

	void FChatConnections::SignOut(EAgentMcpChatProvider Provider)
	{
		FString Error;
		const bool bClaude = Provider == EAgentMcpChatProvider::ClaudeCode;
		const FString Executable = bClaude ? FindClaudeCodeExecutable(Error) : FindCodexExecutable(Error);
		if (!CanSignOut(Provider) || Executable.IsEmpty())
		{
			return;
		}
		Run(Provider, Executable, bClaude ? TEXT("auth logout") : TEXT("logout"),
			bClaude ? GetClaudeCodeWorkingDirectory() : GetCodexWorkingDirectory(), 30.0f, [this, Provider](int32, const FString&)
			{
				Refresh(Provider);
			});
	}

	FConnectionStatus FChatConnections::Get(EAgentMcpChatProvider Provider) const
	{
		const FConnectionStatus* Status = Statuses.Find(Provider);
		return Status ? *Status : FConnectionStatus();
	}

	void FChatConnections::Set(EAgentMcpChatProvider Provider, EChatConnectionState State, const FString& Detail)
	{
		FConnectionStatus& Status = Statuses.FindOrAdd(Provider);
		Status.State = State;
		Status.Detail = Detail;
		if (OnChanged)
		{
			OnChanged();
		}
	}

	void FChatConnections::Run(EAgentMcpChatProvider Provider, const FString& Executable, const FString& Arguments, const FString& WorkingDirectory,
		float TimeoutSeconds, TFunction<void(int32 ReturnCode, const FString& Output)> OnExit)
	{
		Set(Provider, EChatConnectionState::Checking, TEXT("확인 중..."));

		const uint32 RunId = NextRunId++;
		RunIds.Add(Provider, RunId);
		const TSharedRef<FString> Output = MakeShared<FString>();
		TWeakPtr<FChatConnections> WeakSelf = AsShared();
		FString Error;
		TSharedPtr<FCliProcess, ESPMode::ThreadSafe> Process = FCliProcess::Launch(Executable, Arguments, WorkingDirectory,
			[Output](const FString& Line) { *Output += StripTerminalCodes(Line) + TEXT("\n"); },
			[WeakSelf, Provider, RunId, Output, OnExit](int32 ReturnCode)
			{
				const TSharedPtr<FChatConnections> This = WeakSelf.Pin();
				if (!This || This->RunIds.FindRef(Provider) != RunId)
				{
					return;
				}
				This->Processes.Remove(Provider);
				OnExit(ReturnCode, *Output);
			},
			Error);
		if (!Process)
		{
			Set(Provider, EChatConnectionState::Failed, Error);
			return;
		}
		Processes.Add(Provider, Process);

		// A check that hangs (for example a sign-in that waits for the browser) is ended and reported.
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakSelf, Provider, RunId](float)
		{
			const TSharedPtr<FChatConnections> This = WeakSelf.Pin();
			if (This && This->RunIds.FindRef(Provider) == RunId && This->Processes.Contains(Provider))
			{
				This->Processes.Remove(Provider);
				This->RunIds.Remove(Provider);
				This->Set(Provider, EChatConnectionState::Failed, TEXT("응답 없음 (로그인이 필요할 수 있음)"));
			}
			return false;
		}), TimeoutSeconds);
	}
}
