#include "AgentMcpChatSession.h"

#include "AgentMcpChatAntigravity.h"
#include "AgentMcpChatClaudeCode.h"
#include "AgentMcpChatCliProcess.h"
#include "AgentMcpChatCodex.h"
#include "AgentMcpChatHistory.h"
#include "AgentMcpChatMcpClient.h"
#include "AgentMcpChatPermissions.h"
#include "AgentMcpCompat.h"

#include "Dom/JsonValue.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY(LogAgentMcpChat);

namespace UE::AgentMcp::Chat
{
	namespace SessionPrivate
	{
		constexpr int32 MaxShownArgumentChars = 300;

		FString DescribeCall(const FToolCall& Call)
		{
			FString Arguments = Call.Arguments.IsValid() ? UE::AgentMcp::Compat::JsonObjectToString(Call.Arguments.ToSharedRef()) : TEXT("(잘못된 인자)");
			if (Arguments.Len() > MaxShownArgumentChars)
			{
				Arguments = Arguments.Left(MaxShownArgumentChars) + TEXT("...");
			}
			return Call.Name + TEXT(" ") + Arguments;
		}

		/** The text of a tool_result content, which is either a string or a list of content blocks. */
		FString GetToolResultText(const TSharedPtr<FJsonObject>& Block)
		{
			FString Text;
			if (Block->TryGetStringField(TEXT("content"), Text))
			{
				return Text;
			}
			TArray<FString> Parts;
			const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
			if (Block->TryGetArrayField(TEXT("content"), Items))
			{
				for (const TSharedPtr<FJsonValue>& Item : *Items)
				{
					const TSharedPtr<FJsonObject>* ItemObject = nullptr;
					FString ItemText;
					if (Item->TryGetObject(ItemObject) && (*ItemObject)->TryGetStringField(TEXT("text"), ItemText))
					{
						Parts.Add(ItemText);
					}
				}
			}
			return FString::Join(Parts, TEXT("\n"));
		}

		/** Adds what to do when Claude Code reports a login problem. */
		FString DescribeClaudeCodeError(const FString& Text)
		{
			static const TCHAR* const LoginWords[] = { TEXT("login"), TEXT("log in"), TEXT("authenticat"), TEXT("401"), TEXT("API key"), TEXT("OAuth") };
			for (const TCHAR* Word : LoginWords)
			{
				if (Text.Contains(Word))
				{
					return Text + TEXT(" 패널의 '계정'에서 Claude Code에 로그인하세요.");
				}
			}
			return Text;
		}
	}

	FChatSession::FChatSession() = default;

	FChatSession::~FChatSession()
	{
		OnEntry = nullptr;
		OnStateChanged = nullptr;
		Cancel();
		if (ModelListRequest.IsValid())
		{
			ModelListRequest->OnProcessRequestComplete().Unbind();
			ModelListRequest->CancelRequest();
		}
	}

	bool FChatSession::Send(const FString& Text)
	{
		const FString Question = Text.TrimStartAndEnd();
		if (Question.IsEmpty() || bBusy)
		{
			return false;
		}
		FChatApprovals::Get().BeginQuestion();

		const EAgentMcpChatProvider Kind = GetDefault<UAgentMcpChatSettings>()->Provider;
		const bool bClaudeCode = Kind == EAgentMcpChatProvider::ClaudeCode;
		const bool bAntigravity = Kind == EAgentMcpChatProvider::Antigravity;
		const bool bCodex = Kind == EAgentMcpChatProvider::Codex;
		const bool bCli = bClaudeCode || bAntigravity || bCodex;
		// A conversation of another provider cannot be carried over.
		const bool bHadOtherConversation = (Provider && bCli) || (!bClaudeCode && !ClaudeCodeSessionId.IsEmpty())
			|| (!bAntigravity && !AntigravityConversationId.IsEmpty()) || (!bCodex && !CodexThreadId.IsEmpty());
		if (bHadOtherConversation)
		{
			Emit(EChatEntryKind::Status, FString(), FString::Printf(TEXT("%s(으)로 바꿔서 새 대화를 시작합니다."), *GetProviderLabel(Kind)));
		}
		if (!bClaudeCode)
		{
			ClaudeCodeSessionId.Reset();
			ClaudeCodeModelInUse.Reset();
		}
		if (!bAntigravity)
		{
			AntigravityConversationId.Reset();
			AntigravityModelInUse.Reset();
		}
		if (!bCodex)
		{
			CodexThreadId.Reset();
		}

		if (bCli)
		{
			Provider.Reset();
			bCliMode = true;
			Emit(EChatEntryKind::User, TEXT("나"), Question);

			++Generation;
			bBusy = true;
			SetActivity(TEXT("에디터 도구에 연결하는 중..."));
			EnsureConnected([this, Question, bClaudeCode, bCodex]()
			{
				if (bClaudeCode)
				{
					LaunchClaudeCode(Question);
				}
				else if (bCodex)
				{
					StartCodexTurn(Question);
				}
				else
				{
					StartAntigravityTurn(Question);
				}
			});
			return true;
		}
		bCliMode = false;

		FString Error;
		if (!PrepareProvider(Error))
		{
			Emit(EChatEntryKind::Error, TEXT("오류"), Error);
			return false;
		}

		SafeHistoryLength = Provider->GetHistoryLength();
		Provider->AddUserText(Question);
		Emit(EChatEntryKind::User, TEXT("나"), Question);

		++Generation;
		Rounds = 0;
		bBusy = true;
		SetActivity(TEXT("에디터 도구에 연결하는 중..."));

		EnsureConnected([this]() { RequestTurn(); });
		return true;
	}

	void FChatSession::Cancel()
	{
		++Generation;
		if (PendingRequest.IsValid())
		{
			PendingRequest->OnProcessRequestComplete().Unbind();
			PendingRequest->CancelRequest();
			PendingRequest.Reset();
		}
		if (Mcp)
		{
			Mcp->CancelAll();
		}
		// Destroying the process ends the CLI and every process it started.
		CliProcess.Reset();
		FChatApprovals::Get().DenyAll(TEXT("The user stopped the answer."));
		bAwaitingInput = false;
		if (bBusy)
		{
			bBusy = false;
			if (Provider)
			{
				Provider->TruncateHistory(SafeHistoryLength);
				Emit(EChatEntryKind::Status, FString(), TEXT("중지했습니다. 이 질문은 대화 기록에서 뺐습니다."));
			}
			else
			{
				Emit(EChatEntryKind::Status, FString(), TEXT("중지했습니다."));
			}
			SetActivity(FString());
		}
	}

	void FChatSession::Reset()
	{
		Cancel();
		Provider.Reset();
		Mcp.Reset();
		ClaudeCodeSessionId.Reset();
		ClaudeCodeModelInUse.Reset();
		AntigravityConversationId.Reset();
		AntigravityModelInUse.Reset();
		CodexThreadId.Reset();
		PendingApiHistory.Reset();
	}

	void FChatSession::FetchModels(TFunction<void(const TArray<FString>& Models, const FString& Error)> OnDone)
	{
		const EAgentMcpChatProvider Kind = GetDefault<UAgentMcpChatSettings>()->Provider;
		if (Kind == EAgentMcpChatProvider::Antigravity || Kind == EAgentMcpChatProvider::Codex)
		{
			// The models depend on the account, so they come from the CLI rather than a fixed list.
			const bool bCodex = Kind == EAgentMcpChatProvider::Codex;
			FString Error;
			const FString Executable = bCodex ? FindCodexExecutable(Error) : FindAntigravityExecutable(Error);
			if (Executable.IsEmpty())
			{
				OnDone({}, Error);
				return;
			}
			const TSharedRef<FString> Output = MakeShared<FString>();
			TWeakPtr<FChatSession> WeakSelf = AsShared();
			ModelListProcess = FCliProcess::Launch(Executable, bCodex ? TEXT("debug models") : TEXT("models"),
				bCodex ? GetCodexWorkingDirectory() : GetAntigravityWorkingDirectory(),
				[Output](const FString& Line) { *Output += StripTerminalCodes(Line) + TEXT("\n"); },
				[WeakSelf, Output, OnDone, bCodex](int32 ReturnCode)
				{
					const TSharedPtr<FChatSession> This = WeakSelf.Pin();
					if (!This)
					{
						return;
					}
					This->ModelListProcess.Reset();
					TArray<FChatModel>& Models = bCodex ? This->CodexModels : This->AntigravityModels;
					Models = bCodex ? ParseCodexModels(*Output) : ParseAntigravityModels(*Output);
					TArray<FString> Ids;
					for (const FChatModel& Model : Models)
					{
						Ids.Add(Model.Id);
					}
					if (Ids.Num() == 0)
					{
						OnDone({}, bCodex ? TEXT("ChatGPT 모델 목록을 받지 못했습니다. '계정'에서 로그인 상태를 확인하세요.")
							: TEXT("Gemini 모델 목록을 받지 못했습니다. '계정'에서 로그인 상태를 확인하세요."));
						return;
					}
					Ids.Insert(FString(), 0);
					OnDone(Ids, FString());
				},
				Error);
			if (!ModelListProcess)
			{
				OnDone({}, Error);
			}
			return;
		}
		if (Kind == EAgentMcpChatProvider::ClaudeCode)
		{
			TArray<FString> Ids;
			for (const FChatChoice& Choice : GetClaudeCodeModelChoices())
			{
				Ids.Add(Choice.Id);
			}
			OnDone(Ids, FString());
			return;
		}

		FProviderConfig Config;
		FString Error;
		// The list needs the URL and key but not a model, so only those failures stop it.
		if (!ReadProviderConfig(Kind, Config, Error)
			&& (Config.BaseUrl.IsEmpty() || (Config.ApiKey.IsEmpty() && Kind != EAgentMcpChatProvider::OpenAICompatible)))
		{
			OnDone({}, Error);
			return;
		}

		if (ModelListRequest.IsValid())
		{
			ModelListRequest->OnProcessRequestComplete().Unbind();
			ModelListRequest->CancelRequest();
		}

		const TSharedPtr<IChatProvider> ListProvider(MakeProvider(Config).Release());
		FHttpRequestRef Request = ListProvider->MakeModelListRequest();
		TWeakPtr<FChatSession> WeakSelf = AsShared();
		Request->OnProcessRequestComplete().BindLambda(
			[WeakSelf, ListProvider, OnDone](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
			{
				if (const TSharedPtr<FChatSession> This = WeakSelf.Pin())
				{
					This->ModelListRequest.Reset();
				}
				if (!bConnectedSuccessfully || !Response.IsValid())
				{
					OnDone({}, TEXT("모델 목록을 불러오지 못했습니다. 네트워크 연결을 확인하세요."));
					return;
				}
				FString ListError;
				const TArray<FString> Models = ListProvider->ParseModelList(Response->GetResponseCode(), Response->GetContentAsString(), ListError);
				OnDone(Models, ListError);
			});
		ModelListRequest = Request;
		Request->ProcessRequest();
	}

	void FChatSession::Emit(EChatEntryKind Kind, const FString& Speaker, const FString& Text)
	{
		if (Kind == EChatEntryKind::Error)
		{
			UE_LOG(LogAgentMcpChat, Warning, TEXT("%s"), *Text);
		}
		if (OnEntry)
		{
			OnEntry(FChatEntry{ Kind, Speaker, Text });
		}
	}

	void FChatSession::SetActivity(const FString& InActivity)
	{
		Activity = InActivity;
		if (OnStateChanged)
		{
			OnStateChanged();
		}
	}

	bool FChatSession::PrepareProvider(FString& OutError)
	{
		const EAgentMcpChatProvider Kind = GetDefault<UAgentMcpChatSettings>()->Provider;
		FProviderConfig Config;
		if (!ReadProviderConfig(Kind, Config, OutError))
		{
			return false;
		}

		if (!Provider || Provider->GetConfig().Provider != Kind)
		{
			// Each API keeps its history in its own format, so a different provider starts over.
			const bool bHadConversation = Provider && Provider->GetHistoryLength() > 0;
			Provider = MakeProvider(Config);
			if (bHadConversation)
			{
				Emit(EChatEntryKind::Status, FString(), FString::Printf(TEXT("%s(으)로 바꿔서 새 대화를 시작합니다."), *GetProviderLabel(Kind)));
			}
			else if (PendingApiHistory.Num() > 0)
			{
				// A saved conversation goes on where it stopped.
				Provider->SetHistory(PendingApiHistory);
			}
			PendingApiHistory.Reset();
		}
		else
		{
			Provider->SetConfig(Config);
		}
		return true;
	}

	void FChatSession::EnsureConnected(TFunction<void()> OnReady)
	{
		// The tool list depends on the permission mode, so a changed mode connects again.
		const EAgentMcpChatMode Mode = GetDefault<UAgentMcpChatSettings>()->Mode;
		if (Mcp && Mcp->IsConnected() && Mcp->GetMode() == Mode)
		{
			OnReady();
			return;
		}

		Mcp = MakeShared<FMcpClient>();
		TWeakPtr<FChatSession> WeakSelf = AsShared();
		const uint32 ExpectedGeneration = Generation;
		Mcp->Connect(Mode, [WeakSelf, ExpectedGeneration, OnReady](bool bOk, const FString& Error)
		{
			const TSharedPtr<FChatSession> This = WeakSelf.Pin();
			if (!This || This->Generation != ExpectedGeneration)
			{
				return;
			}
			if (!bOk)
			{
				This->Fail(Error);
				return;
			}
			UE_LOG(LogAgentMcpChat, Log, TEXT("Connected to %s with %d tools (%d hidden by the permission mode)."), *This->Mcp->GetEndpoint(),
				This->Mcp->GetTools().Num(), This->Mcp->GetBlockedToolNames().Num());
			OnReady();
		});
	}

	void FChatSession::RequestTurn()
	{
		const int32 MaxRounds = GetDefault<UAgentMcpChatSettings>()->MaxToolRounds;
		if (++Rounds > MaxRounds)
		{
			Fail(FString::Printf(TEXT("모델 요청 %d번 안에 최종 답변이 나오지 않아 중지했습니다. 질문 범위를 좁히거나 설정에서 최대 도구 라운드를 늘리세요."), MaxRounds));
			return;
		}

		SetActivity(FString::Printf(TEXT("%s 응답을 기다리는 중..."), *GetProviderLabel(Provider->GetConfig().Provider)));

		FHttpRequestRef Request = Provider->MakeTurnRequest(MakeSystemPrompt(/*bHasOwnTools=*/false), Mcp->GetTools());
		TWeakPtr<FChatSession> WeakSelf = AsShared();
		const uint32 ExpectedGeneration = Generation;
		Request->OnProcessRequestComplete().BindLambda(
			[WeakSelf, ExpectedGeneration](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
			{
				const TSharedPtr<FChatSession> This = WeakSelf.Pin();
				if (!This || This->Generation != ExpectedGeneration)
				{
					return;
				}
				This->PendingRequest.Reset();
				if (!bConnectedSuccessfully || !Response.IsValid())
				{
					This->Fail(FString::Printf(TEXT("%s에서 응답이 없습니다. 네트워크 연결을 확인하거나 설정에서 요청 제한 시간을 늘리세요."),
						*GetProviderLabel(This->Provider->GetConfig().Provider)));
					return;
				}
				This->HandleTurn(Response->GetResponseCode(), Response->GetContentAsString());
			});
		PendingRequest = Request;
		Request->ProcessRequest();
	}

	void FChatSession::HandleTurn(int32 ResponseCode, const FString& Body)
	{
		const FModelReply Reply = Provider->ParseTurnResponse(ResponseCode, Body);
		if (!Reply.bOk)
		{
			Fail(Reply.Error);
			return;
		}

		if (!Reply.Text.IsEmpty())
		{
			Emit(EChatEntryKind::Assistant, GetProviderLabel(Provider->GetConfig().Provider), Reply.Text);
		}
		if (!Reply.Note.IsEmpty())
		{
			Emit(EChatEntryKind::Status, FString(), Reply.Note);
		}

		if (Reply.ToolCalls.Num() == 0)
		{
			Finish();
			return;
		}
		RunToolCall(MakeShared<TArray<FToolCall>>(Reply.ToolCalls), 0, MakeShared<TArray<FToolOutcome>>());
	}

	void FChatSession::RunToolCall(TSharedRef<TArray<FToolCall>> Calls, int32 Index, TSharedRef<TArray<FToolOutcome>> Outcomes)
	{
		if (Index >= Calls->Num())
		{
			Provider->AddToolOutcomes(*Outcomes);
			RequestTurn();
			return;
		}

		const FToolCall& Call = (*Calls)[Index];
		Emit(EChatEntryKind::Tool, TEXT("도구"), SessionPrivate::DescribeCall(Call));
		SetActivity(FString::Printf(TEXT("%s 실행 중..."), *Call.Name));

		FToolOutcome Outcome;
		Outcome.CallId = Call.Id;
		Outcome.Name = Call.Name;

		if (!Call.Arguments.IsValid())
		{
			Outcome.Text = TEXT("The arguments were not a valid JSON object.");
			Outcome.bIsError = true;
			Outcomes->Add(MoveTemp(Outcome));
			RunToolCall(Calls, Index + 1, Outcomes);
			return;
		}

		// Tool calls run one at a time: the server runs them on the game thread anyway.
		TWeakPtr<FChatSession> WeakSelf = AsShared();
		const uint32 ExpectedGeneration = Generation;
		Mcp->CallTool(Call.Name, Call.Arguments.ToSharedRef(),
			[WeakSelf, ExpectedGeneration, Calls, Index, Outcomes, Outcome](const FString& Text, bool bIsError) mutable
			{
				const TSharedPtr<FChatSession> This = WeakSelf.Pin();
				if (!This || This->Generation != ExpectedGeneration)
				{
					return;
				}
				if (bIsError)
				{
					This->Emit(EChatEntryKind::Tool, TEXT("도구"), FString::Printf(TEXT("%s 실패: %s"), *Outcome.Name, *Text.Left(500)));
				}
				Outcome.Text = Text;
				Outcome.bIsError = bIsError;
				Outcomes->Add(MoveTemp(Outcome));
				This->RunToolCall(Calls, Index + 1, Outcomes);
			});
	}

	void FChatSession::Fail(const FString& Error)
	{
		if (Provider)
		{
			Provider->TruncateHistory(SafeHistoryLength);
		}
		bBusy = false;
		Emit(EChatEntryKind::Error, TEXT("오류"), Error);
		SetActivity(FString());
	}

	void FChatSession::Finish()
	{
		bBusy = false;
		SetActivity(FString());
	}

	void FChatSession::LaunchClaudeCode(const FString& Question)
	{
		FClaudeCodeLaunch Launch;
		FString Error;
		if (!PrepareClaudeCodeLaunch(Question, MakeSystemPrompt(/*bHasOwnTools=*/false), ClaudeCodeSessionId, *Mcp, Launch, Error))
		{
			Fail(Error);
			return;
		}

		bCliGotResult = false;
		CliOtherOutput.Reset();
		SetActivity(TEXT("Claude Code 응답을 기다리는 중..."));
		UE_LOG(LogAgentMcpChat, Log, TEXT("Starting Claude Code: %s (%s)"), *Launch.Executable,
			ClaudeCodeSessionId.IsEmpty() ? TEXT("new conversation") : *ClaudeCodeSessionId);

		TWeakPtr<FChatSession> WeakSelf = AsShared();
		const uint32 ExpectedGeneration = Generation;
		const uint32 LaunchId = ++CliLaunchId;
		CliProcess = FCliProcess::Launch(Launch.Executable, Launch.Arguments, Launch.WorkingDirectory,
			[WeakSelf, ExpectedGeneration, LaunchId](const FString& Line)
			{
				const TSharedPtr<FChatSession> This = WeakSelf.Pin();
				if (This && This->Generation == ExpectedGeneration && This->CliLaunchId == LaunchId)
				{
					This->HandleClaudeCodeLine(Line);
				}
			},
			[WeakSelf, ExpectedGeneration, LaunchId](int32 ReturnCode)
			{
				const TSharedPtr<FChatSession> This = WeakSelf.Pin();
				if (This && This->Generation == ExpectedGeneration && This->CliLaunchId == LaunchId)
				{
					This->HandleCliExit(ReturnCode);
				}
			},
			Error);
		if (!CliProcess)
		{
			Fail(Error);
		}
	}

	void FChatSession::HandleClaudeCodeLine(const FString& Line)
	{
		using namespace SessionPrivate;

		TSharedPtr<FJsonObject> Message;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
		if (!Line.StartsWith(TEXT("{")) || !FJsonSerializer::Deserialize(Reader, Message) || !Message.IsValid())
		{
			UE_LOG(LogAgentMcpChat, Log, TEXT("Claude Code: %s"), *Line);
			if (CliOtherOutput.Len() < 2000)
			{
				CliOtherOutput += Line + TEXT("\n");
			}
			return;
		}

		FString SessionId;
		if (Message->TryGetStringField(TEXT("session_id"), SessionId) && !SessionId.IsEmpty())
		{
			ClaudeCodeSessionId = SessionId;
		}

		FString Type;
		Message->TryGetStringField(TEXT("type"), Type);
		FString Subtype;
		Message->TryGetStringField(TEXT("subtype"), Subtype);

		if (Type == TEXT("system") && Subtype == TEXT("init"))
		{
			Message->TryGetStringField(TEXT("model"), ClaudeCodeModelInUse);
			const TArray<TSharedPtr<FJsonValue>>* Servers = nullptr;
			if (Message->TryGetArrayField(TEXT("mcp_servers"), Servers))
			{
				for (const TSharedPtr<FJsonValue>& ServerValue : *Servers)
				{
					const TSharedPtr<FJsonObject>* Server = nullptr;
					FString Name;
					FString Status;
					if (!ServerValue->TryGetObject(Server) || !(*Server)->TryGetStringField(TEXT("name"), Name) || Name != ClaudeCodeServerName)
					{
						continue;
					}
					(*Server)->TryGetStringField(TEXT("status"), Status);
					UE_LOG(LogAgentMcpChat, Log, TEXT("Claude Code MCP server '%s': %s"), *Name, *Status);
					if (Status != TEXT("connected"))
					{
						Emit(EChatEntryKind::Status, FString(), FString::Printf(TEXT("Claude Code가 에디터 도구에 연결하지 못했습니다(%s). 도구 없이 답할 수 있습니다."), *Status));
					}
				}
			}
			return;
		}

		const TSharedPtr<FJsonObject>* Body = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
		const bool bHasContent = Message->TryGetObjectField(TEXT("message"), Body) && (*Body)->TryGetArrayField(TEXT("content"), Content);

		FString Model;
		if (bHasContent && (*Body)->TryGetStringField(TEXT("model"), Model) && Model == TEXT("<synthetic>"))
		{
			// Claude Code's own notices, such as "Not logged in"; the result line reports the same text as an error.
			return;
		}

		if (Type == TEXT("assistant") && bHasContent)
		{
			TArray<FString> Texts;
			for (const TSharedPtr<FJsonValue>& BlockValue : *Content)
			{
				const TSharedPtr<FJsonObject>* Block = nullptr;
				FString BlockType;
				if (!BlockValue->TryGetObject(Block) || !(*Block)->TryGetStringField(TEXT("type"), BlockType))
				{
					continue;
				}
				if (BlockType == TEXT("text"))
				{
					FString Text;
					(*Block)->TryGetStringField(TEXT("text"), Text);
					Texts.Add(Text);
				}
				else if (BlockType == TEXT("tool_use"))
				{
					FToolCall Call;
					(*Block)->TryGetStringField(TEXT("name"), Call.Name);
					Call.Name = GetClaudeCodeToolDisplayName(Call.Name);
					const TSharedPtr<FJsonObject>* Input = nullptr;
					if ((*Block)->TryGetObjectField(TEXT("input"), Input))
					{
						Call.Arguments = *Input;
					}
					Emit(EChatEntryKind::Tool, TEXT("도구"), DescribeCall(Call));
					SetActivity(FString::Printf(TEXT("%s 실행 중..."), *Call.Name));
				}
			}
			const FString Text = FString::Join(Texts, TEXT("\n\n")).TrimStartAndEnd();
			if (!Text.IsEmpty())
			{
				Emit(EChatEntryKind::Assistant, ClaudeCodeModelInUse.IsEmpty() ? TEXT("Claude") : TEXT("Claude · ") + ClaudeCodeModelInUse, Text);
			}
			return;
		}

		if (Type == TEXT("user") && bHasContent)
		{
			for (const TSharedPtr<FJsonValue>& BlockValue : *Content)
			{
				const TSharedPtr<FJsonObject>* Block = nullptr;
				FString BlockType;
				bool bIsError = false;
				if (BlockValue->TryGetObject(Block) && (*Block)->TryGetStringField(TEXT("type"), BlockType) && BlockType == TEXT("tool_result")
					&& (*Block)->TryGetBoolField(TEXT("is_error"), bIsError) && bIsError)
				{
					Emit(EChatEntryKind::Tool, TEXT("도구"), TEXT("도구 실패: ") + GetToolResultText(*Block).Left(500));
				}
			}
			SetActivity(TEXT("Claude Code 응답을 기다리는 중..."));
			return;
		}

		if (Type == TEXT("result"))
		{
			bCliGotResult = true;
			bool bIsError = false;
			Message->TryGetBoolField(TEXT("is_error"), bIsError);
			if (bIsError || Subtype != TEXT("success"))
			{
				FString Text;
				Message->TryGetStringField(TEXT("result"), Text);
				if (Text.IsEmpty())
				{
					Text = Subtype;
				}
				if (Text.Contains(TEXT("No conversation found")))
				{
					// The saved conversation is gone; the next question starts a new one.
					ClaudeCodeSessionId.Reset();
				}
				Fail(TEXT("Claude Code 오류: ") + DescribeClaudeCodeError(Text));
				return;
			}
			Finish();
		}
	}

	void FChatSession::HandleCliExit(int32 ReturnCode)
	{
		CliProcess.Reset();
		if (!bBusy || bCliGotResult)
		{
			return;
		}
		const FString Output = CliOtherOutput.TrimStartAndEnd().Left(800);
		if (GetDefault<UAgentMcpChatSettings>()->Provider == EAgentMcpChatProvider::Codex)
		{
			Fail(FString::Printf(TEXT("ChatGPT(Codex)가 답변 없이 종료되었습니다(종료 코드 %d). %s"), ReturnCode,
				Output.IsEmpty() ? TEXT("패널의 '계정'에서 ChatGPT의 설치와 로그인 상태를 확인하세요.") : *Output));
			return;
		}
		if (GetDefault<UAgentMcpChatSettings>()->Provider == EAgentMcpChatProvider::Antigravity)
		{
			Fail(FString::Printf(TEXT("Gemini(Antigravity)가 답변 없이 종료되었습니다(종료 코드 %d). %s"), ReturnCode,
				Output.IsEmpty() ? TEXT("패널의 '계정'에서 Gemini의 설치와 로그인 상태를 확인하세요.") : *Output));
			return;
		}
		Fail(FString::Printf(TEXT("Claude Code가 답변 없이 종료되었습니다(종료 코드 %d). %s"), ReturnCode,
			*SessionPrivate::DescribeClaudeCodeError(Output.IsEmpty() ? TEXT("패널의 '계정'에서 Claude Code의 설치와 로그인 상태를 확인하세요.") : Output)));
	}

	void FChatSession::SetUpClaudeCode()
	{
		if (bBusy)
		{
			return;
		}
		++Generation;
		bBusy = true;

		FString Error;
		const FString Executable = FindClaudeCodeExecutable(Error);
		if (Executable.IsEmpty())
		{
			InstallClaudeCode();
			return;
		}
		CheckClaudeCodeLogin(Executable, /*bSignInIfNeeded=*/true);
	}

	void FChatSession::InstallClaudeCode()
	{
		FString Executable;
		FString Arguments;
		if (!GetClaudeCodeInstallCommand(Executable, Arguments))
		{
			Fail(TEXT("이 PC에서는 Claude Code를 자동으로 설치할 수 없습니다. Claude Code를 직접 설치한 뒤 다시 눌러 주세요."));
			return;
		}

		Emit(EChatEntryKind::Status, FString(), TEXT("Claude Code를 설치합니다. 1~2분 걸릴 수 있습니다."));
		SetActivity(TEXT("Claude Code 설치 중..."));
		RunSetupProcess(Executable, Arguments, /*bShowUrls=*/false, [this](int32 ReturnCode, const FString& Output)
		{
			FString Error;
			const FString Installed = FindClaudeCodeExecutable(Error);
			if (Installed.IsEmpty())
			{
				Fail(FString::Printf(TEXT("Claude Code를 설치하지 못했습니다(종료 코드 %d). %s"), ReturnCode, *Output.Right(600)));
				return;
			}
			Emit(EChatEntryKind::Status, FString(), TEXT("Claude Code를 설치했습니다."));
			CheckClaudeCodeLogin(Installed, /*bSignInIfNeeded=*/true);
		});
	}

	void FChatSession::CheckClaudeCodeLogin(const FString& Executable, bool bSignInIfNeeded)
	{
		SetActivity(TEXT("Claude Code 로그인 상태를 확인하는 중..."));
		RunSetupProcess(Executable, TEXT("auth status --json"), /*bShowUrls=*/false,
			[this, Executable, bSignInIfNeeded](int32 ReturnCode, const FString& Output)
			{
				// The status is a JSON object, possibly after other lines.
				TSharedPtr<FJsonObject> Status;
				const int32 Start = Output.Find(TEXT("{"));
				if (Start != INDEX_NONE)
				{
					const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Output.RightChop(Start));
					FJsonSerializer::Deserialize(Reader, Status);
				}
				bool bLoggedIn = false;
				if (Status.IsValid())
				{
					Status->TryGetBoolField(TEXT("loggedIn"), bLoggedIn);
				}

				if (bLoggedIn)
				{
					FString Email;
					Status->TryGetStringField(TEXT("email"), Email);
					Emit(EChatEntryKind::Status, FString(), Email.IsEmpty()
						? TEXT("Claude Code에 로그인되어 있습니다. 이제 질문하면 됩니다.")
						: FString::Printf(TEXT("Claude Code에 %s(으)로 로그인되어 있습니다. 이제 질문하면 됩니다."), *Email));
					Finish();
					return;
				}
				if (!Status.IsValid())
				{
					Fail(FString::Printf(TEXT("Claude Code 로그인 상태를 읽지 못했습니다(종료 코드 %d). %s"), ReturnCode, *Output.Left(600)));
					return;
				}
				if (bSignInIfNeeded)
				{
					SignInClaudeCode(Executable);
					return;
				}
				Fail(TEXT("Claude Code 로그인이 끝나지 않았습니다. '계정'에서 Claude Code의 '로그인'을 다시 눌러 주세요."));
			});
	}

	void FChatSession::SignInClaudeCode(const FString& Executable)
	{
		Emit(EChatEntryKind::Status, FString(),
			TEXT("브라우저에서 Claude 로그인 페이지가 열립니다. 개인 Claude 계정으로 로그인하고 승인하면 자동으로 이어집니다."));
		SetActivity(TEXT("브라우저에서 로그인을 기다리는 중... (취소하려면 중지)"));
		RunSetupProcess(Executable, TEXT("auth login --claudeai"), /*bShowUrls=*/true, [this, Executable](int32 ReturnCode, const FString& Output)
		{
			CheckClaudeCodeLogin(Executable, /*bSignInIfNeeded=*/false);
		});
	}

	void FChatSession::RunSetupProcess(const FString& Executable, const FString& Arguments, bool bShowUrls,
		TFunction<void(int32 ReturnCode, const FString& Output)> OnExit, bool bWithInput, TFunction<void(const FString& Line)> OnLine)
	{
		const TSharedRef<FString> Output = MakeShared<FString>();
		TWeakPtr<FChatSession> WeakSelf = AsShared();
		const uint32 ExpectedGeneration = Generation;
		const uint32 LaunchId = ++CliLaunchId;
		const EAgentMcpChatProvider Kind = GetDefault<UAgentMcpChatSettings>()->Provider;
		const FString WorkingDirectory = Kind == EAgentMcpChatProvider::Antigravity ? GetAntigravityWorkingDirectory()
			: Kind == EAgentMcpChatProvider::Codex ? GetCodexWorkingDirectory() : GetClaudeCodeWorkingDirectory();
		FString Error;
		CliProcess = FCliProcess::Launch(Executable, Arguments, WorkingDirectory,
			[WeakSelf, ExpectedGeneration, LaunchId, Output, bShowUrls, OnLine](const FString& RawLine)
			{
				const TSharedPtr<FChatSession> This = WeakSelf.Pin();
				if (!This || This->Generation != ExpectedGeneration || This->CliLaunchId != LaunchId)
				{
					return;
				}
				const FString Line = StripTerminalCodes(RawLine);
				UE_LOG(LogAgentMcpChat, Log, TEXT("CLI setup: %s"), *Line);
				if (OnLine)
				{
					OnLine(Line);
				}
				if (Output->Len() < 8000)
				{
					*Output += Line + TEXT("\n");
				}
				// If the browser does not open, the person can open the sign-in address shown here.
				if (bShowUrls && Line.Contains(TEXT("https://")))
				{
					This->Emit(EChatEntryKind::Status, FString(), TEXT("브라우저가 열리지 않으면 이 주소를 여세요: ") + Line);
				}
			},
			[WeakSelf, ExpectedGeneration, LaunchId, Output, OnExit](int32 ReturnCode)
			{
				const TSharedPtr<FChatSession> This = WeakSelf.Pin();
				if (!This || This->Generation != ExpectedGeneration || This->CliLaunchId != LaunchId)
				{
					return;
				}
				This->CliProcess.Reset();
				This->bAwaitingInput = false;
				OnExit(ReturnCode, *Output);
			},
			Error, bWithInput);
		if (!CliProcess)
		{
			Fail(Error);
		}
	}

	void FChatSession::StartAntigravityTurn(const FString& Question)
	{
		AntigravityRetries = 0;
		// agy has no system prompt option, so the instructions lead the first message; the conversation keeps them after that.
		LaunchAntigravity(AntigravityConversationId.IsEmpty()
			? MakeSystemPrompt(/*bHasOwnTools=*/true) + TEXT("\n---\n\n") + Question
			: Question);
	}

	void FChatSession::LaunchAntigravity(const FString& Prompt)
	{
		FAntigravityLaunch Launch;
		FString Error;
		if (!PrepareAntigravityLaunch(Prompt, AntigravityConversationId, *Mcp, Launch, Error))
		{
			Fail(Error);
			return;
		}

		bCliGotResult = false;
		bAntigravityAnswered = false;
		AntigravityStepText.Reset();
		CliOtherOutput.Reset();
		SetActivity(TEXT("Gemini 응답을 기다리는 중..."));
		UE_LOG(LogAgentMcpChat, Log, TEXT("Starting Antigravity: %s (%s)"), *Launch.Executable,
			AntigravityConversationId.IsEmpty() ? TEXT("new conversation") : *AntigravityConversationId);

		TWeakPtr<FChatSession> WeakSelf = AsShared();
		const uint32 ExpectedGeneration = Generation;
		const uint32 LaunchId = ++CliLaunchId;
		CliProcess = FCliProcess::Launch(Launch.Executable, Launch.Arguments, Launch.WorkingDirectory,
			[WeakSelf, ExpectedGeneration, LaunchId](const FString& Line)
			{
				const TSharedPtr<FChatSession> This = WeakSelf.Pin();
				if (This && This->Generation == ExpectedGeneration && This->CliLaunchId == LaunchId)
				{
					This->HandleAntigravityLine(Line);
				}
			},
			[WeakSelf, ExpectedGeneration, LaunchId](int32 ReturnCode)
			{
				const TSharedPtr<FChatSession> This = WeakSelf.Pin();
				if (This && This->Generation == ExpectedGeneration && This->CliLaunchId == LaunchId)
				{
					This->HandleCliExit(ReturnCode);
				}
			},
			Error);
		if (!CliProcess)
		{
			Fail(Error);
		}
	}

	void FChatSession::HandleAntigravityLine(const FString& Line)
	{
		using namespace SessionPrivate;

		TSharedPtr<FJsonObject> Message;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
		if (!Line.StartsWith(TEXT("{")) || !FJsonSerializer::Deserialize(Reader, Message) || !Message.IsValid())
		{
			UE_LOG(LogAgentMcpChat, Log, TEXT("Antigravity: %s"), *Line);
			if (CliOtherOutput.Len() < 2000)
			{
				CliOtherOutput += StripTerminalCodes(Line) + TEXT("\n");
			}
			return;
		}

		FString Event;
		Message->TryGetStringField(TEXT("event"), Event);
		const TSharedPtr<FJsonObject>* Payload = nullptr;
		Message->TryGetObjectField(Event, Payload);

		FString ConversationId;
		if ((Message->TryGetStringField(TEXT("conversation_id"), ConversationId) || (Payload && (*Payload)->TryGetStringField(TEXT("conversation_id"), ConversationId)))
			&& !ConversationId.IsEmpty())
		{
			AntigravityConversationId = ConversationId;
		}
		if (!Payload)
		{
			return;
		}
		const TSharedPtr<FJsonObject>& Body = *Payload;
		const FString Speaker = AntigravityModelInUse.IsEmpty() ? FString(TEXT("Gemini")) : TEXT("Gemini · ") + AntigravityModelInUse;

		if (Event == TEXT("init"))
		{
			Body->TryGetStringField(TEXT("model"), AntigravityModelInUse);
			return;
		}

		if (Event == TEXT("step_update"))
		{
			FString StepType;
			FString State;
			int32 StepIndex = 0;
			Body->TryGetStringField(TEXT("step_type"), StepType);
			Body->TryGetStringField(TEXT("state"), State);
			Body->TryGetNumberField(TEXT("step_index"), StepIndex);

			if (StepType == TEXT("agent_response"))
			{
				FString Delta;
				if (Body->TryGetStringField(TEXT("text_delta"), Delta))
				{
					AntigravityStepText.FindOrAdd(StepIndex) += Delta;
				}
				if (State == TEXT("DONE"))
				{
					const FString Text = AntigravityStepText.FindRef(StepIndex).TrimStartAndEnd();
					AntigravityStepText.Remove(StepIndex);
					if (!Text.IsEmpty())
					{
						Emit(EChatEntryKind::Assistant, Speaker, Text);
						bAntigravityAnswered = true;
					}
				}
				return;
			}

			if (StepType == TEXT("tool"))
			{
				FString ToolName;
				Body->TryGetStringField(TEXT("tool_name"), ToolName);
				const TSharedPtr<FJsonObject>* Info = nullptr;
				const TSharedPtr<FJsonObject>* Parameters = nullptr;
				if (Body->TryGetObjectField(TEXT("tool_info"), Info))
				{
					(*Info)->TryGetObjectField(TEXT("parameters"), Parameters);
				}

				FToolCall Call;
				Call.Name = ToolName;
				if (Parameters)
				{
					Call.Arguments = *Parameters;
					FString InnerName;
					const TSharedPtr<FJsonObject>* InnerArguments = nullptr;
					// MCP tools all arrive as call_mcp_tool; show the tool that was really called.
					if (ToolName == TEXT("call_mcp_tool") && (*Parameters)->TryGetStringField(TEXT("ToolName"), InnerName))
					{
						Call.Name = InnerName;
						Call.Arguments = (*Parameters)->TryGetObjectField(TEXT("Arguments"), InnerArguments) ? *InnerArguments : MakeShared<FJsonObject>();
					}
					FString FilePath;
					// Antigravity reads the tool schemas it cached under ~/.gemini; that is its own bookkeeping, not the answer's work.
					if (ToolName == TEXT("view_file") && (*Parameters)->TryGetStringField(TEXT("AbsolutePath"), FilePath) && FilePath.Contains(TEXT(".gemini")))
					{
						return;
					}
				}

				if (State == TEXT("ACTIVE"))
				{
					SetActivity(FString::Printf(TEXT("%s 실행 중..."), *Call.Name));
					return;
				}
				Emit(EChatEntryKind::Tool, TEXT("도구"), DescribeCall(Call));
				const TSharedPtr<FJsonObject>* ToolError = nullptr;
				FString ErrorMessage;
				if (State == TEXT("ERROR") && Info && (*Info)->TryGetObjectField(TEXT("error"), ToolError) && (*ToolError)->TryGetStringField(TEXT("message"), ErrorMessage))
				{
					FString FirstLine;
					if (!ErrorMessage.Split(TEXT("\n"), &FirstLine, nullptr))
					{
						FirstLine = ErrorMessage;
					}
					Emit(EChatEntryKind::Tool, TEXT("도구"), TEXT("도구 실패: ") + FirstLine.Left(300));
				}
				SetActivity(TEXT("Gemini 응답을 기다리는 중..."));
			}
			return;
		}

		if (Event == TEXT("result"))
		{
			bCliGotResult = true;
			FString Status;
			Body->TryGetStringField(TEXT("status"), Status);
			if (Status != TEXT("SUCCESS"))
			{
				FString ErrorText = Status;
				const TSharedPtr<FJsonObject>* ResultError = nullptr;
				if (Body->TryGetObjectField(TEXT("error"), ResultError))
				{
					(*ResultError)->TryGetStringField(TEXT("message"), ErrorText);
				}
				Fail(TEXT("Gemini 오류: ") + ErrorText);
				return;
			}

			FString Response;
			Body->TryGetStringField(TEXT("response"), Response);
			Response.TrimStartAndEndInline();
			if (!bAntigravityAnswered && !Response.IsEmpty())
			{
				Emit(EChatEntryKind::Assistant, Speaker, Response);
				bAntigravityAnswered = true;
			}

			TArray<FString> Denied;
			const TArray<TSharedPtr<FJsonValue>>* DeniedActions = nullptr;
			if (Body->TryGetArrayField(TEXT("denied_actions"), DeniedActions))
			{
				for (const TSharedPtr<FJsonValue>& Value : *DeniedActions)
				{
					const TSharedPtr<FJsonObject>* Action = nullptr;
					FString Name;
					if (Value->TryGetObject(Action) && (*Action)->TryGetStringField(TEXT("display_name"), Name))
					{
						Denied.AddUnique(Name);
					}
				}
			}

			if (!bAntigravityAnswered && Denied.Num() > 0)
			{
				const FString DeniedText = FString::Join(Denied, TEXT(", "));
				// A denied action ends the turn without a reply; ask once more within the allowed tools.
				if (AntigravityRetries < 1)
				{
					++AntigravityRetries;
					Emit(EChatEntryKind::Status, FString(), FString::Printf(TEXT("허용되지 않은 동작(%s)이 거부돼서 허용된 도구로 다시 요청합니다."), *DeniedText));
					LaunchAntigravity(FString::Printf(TEXT(
						"The action you tried (%s) is blocked in this environment, which ended your answer. Do not use run_command, the "
						"browser, web search or file tools again. Use only the agentmcp-chat MCP tools through call_mcp_tool, with "
						"source_find, source_search and source_read for project files, and answer my previous question."), *DeniedText));
					return;
				}
				Fail(FString::Printf(TEXT("Gemini가 허용되지 않은 동작(%s)을 시도해서 답하지 못했습니다. 질문을 조금 바꿔 다시 해 보세요."), *DeniedText));
				return;
			}
			Finish();
		}
	}

	void FChatSession::SetUpAntigravity()
	{
		if (bBusy)
		{
			return;
		}
		++Generation;
		bBusy = true;

		FString Error;
		const FString Executable = FindAntigravityExecutable(Error);
		if (Executable.IsEmpty())
		{
			InstallAntigravity();
			return;
		}
		CheckAntigravityLogin(Executable);
	}

	void FChatSession::InstallAntigravity()
	{
		FString Executable;
		FString Arguments;
		if (!GetAntigravityInstallCommand(Executable, Arguments))
		{
			Fail(TEXT("이 PC에서는 Antigravity CLI를 자동으로 설치할 수 없습니다. 직접 설치한 뒤 다시 눌러 주세요."));
			return;
		}

		Emit(EChatEntryKind::Status, FString(), TEXT("Antigravity CLI(Gemini)를 설치합니다. 1~2분 걸릴 수 있습니다."));
		SetActivity(TEXT("Antigravity CLI 설치 중..."));
		RunSetupProcess(Executable, Arguments, /*bShowUrls=*/false, [this](int32 ReturnCode, const FString& Output)
		{
			FString Error;
			const FString Installed = FindAntigravityExecutable(Error);
			if (Installed.IsEmpty())
			{
				Fail(FString::Printf(TEXT("Antigravity CLI를 설치하지 못했습니다(종료 코드 %d). %s"), ReturnCode, *Output.Right(600)));
				return;
			}
			Emit(EChatEntryKind::Status, FString(), TEXT("Antigravity CLI를 설치했습니다."));
			CheckAntigravityLogin(Installed);
		});
	}

	void FChatSession::CheckAntigravityLogin(const FString& Executable)
	{
		FString Error;
		if (!EnsureAntigravityPermission(Error))
		{
			Fail(Error);
			return;
		}

		Emit(EChatEntryKind::Status, FString(), TEXT("Gemini 로그인 상태를 확인합니다. 로그인이 필요하면 브라우저에 Google 로그인 페이지가 열립니다."));
		SetActivity(TEXT("Gemini 로그인 확인 중... (취소하려면 중지)"));

		// Any plain line during this step is the CLI talking to the person (a sign-in address or a request for the code).
		TWeakPtr<FChatSession> WeakSelf = AsShared();
		auto OnLine = [WeakSelf](const FString& Line)
		{
			const TSharedPtr<FChatSession> This = WeakSelf.Pin();
			if (!This || Line.IsEmpty() || Line.StartsWith(TEXT("{")))
			{
				return;
			}
			This->Emit(EChatEntryKind::Status, FString(), Line.Left(500));
			if (!This->bAwaitingInput)
			{
				This->bAwaitingInput = true;
				This->Emit(EChatEntryKind::Status, FString(),
					TEXT("브라우저에서 Google 계정으로 로그인한 뒤, 화면에 나온 코드를 아래 입력칸에 붙여넣고 Enter를 누르세요."));
			}
		};

		RunSetupProcess(Executable, TEXT("-p \"Reply with exactly: OK\" --output-format json --disable-slash-commands"), /*bShowUrls=*/true,
			[this](int32 ReturnCode, const FString& Output)
			{
				TSharedPtr<FJsonObject> Result;
				const int32 Start = Output.Find(TEXT("{"));
				if (Start != INDEX_NONE)
				{
					const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Output.RightChop(Start));
					FJsonSerializer::Deserialize(Reader, Result);
				}
				FString Status;
				if (Result.IsValid() && Result->TryGetStringField(TEXT("status"), Status) && Status == TEXT("SUCCESS"))
				{
					Emit(EChatEntryKind::Status, FString(), TEXT("Gemini(Antigravity)에 로그인되어 있습니다. 이제 질문하면 됩니다."));
					Finish();
					return;
				}
				Fail(FString::Printf(TEXT("Gemini 로그인을 확인하지 못했습니다(종료 코드 %d). %s"), ReturnCode, *Output.Right(600)));
			},
			/*bWithInput=*/true, OnLine);
	}

	bool FChatSession::SubmitInput(const FString& Text)
	{
		const FString Trimmed = Text.TrimStartAndEnd();
		if (!bAwaitingInput || !CliProcess || Trimmed.IsEmpty())
		{
			return false;
		}
		if (!CliProcess->WriteLine(Trimmed))
		{
			Emit(EChatEntryKind::Error, TEXT("오류"), TEXT("코드를 전달하지 못했습니다. '계정'에서 Gemini의 '로그인'을 다시 눌러 주세요."));
			return false;
		}
		bAwaitingInput = false;
		// The code itself is not shown: it signs in the account.
		Emit(EChatEntryKind::Status, FString(), TEXT("코드를 전달했습니다. 로그인을 확인하는 중입니다."));
		return true;
	}

	FChatSessionState FChatSession::CaptureState() const
	{
		FChatSessionState State;
		State.Provider = GetDefault<UAgentMcpChatSettings>()->Provider;
		State.ClaudeCodeSessionId = ClaudeCodeSessionId;
		State.AntigravityConversationId = AntigravityConversationId;
		State.CodexThreadId = CodexThreadId;
		State.ApiHistory = Provider ? Provider->GetHistory() : PendingApiHistory;
		return State;
	}

	void FChatSession::RestoreState(const FChatSessionState& State)
	{
		Reset();
		UAgentMcpChatSettings* Settings = GetMutableDefault<UAgentMcpChatSettings>();
		if (Settings->Provider != State.Provider)
		{
			Settings->Provider = State.Provider;
			Settings->SaveConfig();
		}
		ClaudeCodeSessionId = State.ClaudeCodeSessionId;
		AntigravityConversationId = State.AntigravityConversationId;
		CodexThreadId = State.CodexThreadId;
		PendingApiHistory = State.ApiHistory;
		bCliMode = State.Provider == EAgentMcpChatProvider::ClaudeCode || State.Provider == EAgentMcpChatProvider::Antigravity
			|| State.Provider == EAgentMcpChatProvider::Codex;
	}

	void FChatSession::StartCodexTurn(const FString& Question)
	{
		// Codex has no system prompt option in exec mode either, so the instructions lead the first message.
		LaunchCodex(CodexThreadId.IsEmpty()
			? MakeSystemPrompt(/*bHasOwnTools=*/false) + TEXT("\nUse only the tools of the agentmcp-chat MCP server; apply_patch and shell ")
				TEXT("commands are not available in this read-only environment.\n---\n\n") + Question
			: Question);
	}

	void FChatSession::LaunchCodex(const FString& Prompt)
	{
		FCodexLaunch Launch;
		FString Error;
		if (!PrepareCodexLaunch(Prompt, CodexThreadId, *Mcp, Launch, Error))
		{
			Fail(Error);
			return;
		}

		bCliGotResult = false;
		bCodexAnswered = false;
		CliOtherOutput.Reset();
		SetActivity(TEXT("ChatGPT 응답을 기다리는 중..."));
		UE_LOG(LogAgentMcpChat, Log, TEXT("Starting Codex: %s (%s)"), *Launch.Executable, CodexThreadId.IsEmpty() ? TEXT("new conversation") : *CodexThreadId);

		TWeakPtr<FChatSession> WeakSelf = AsShared();
		const uint32 ExpectedGeneration = Generation;
		const uint32 LaunchId = ++CliLaunchId;
		CliProcess = FCliProcess::Launch(Launch.Executable, Launch.Arguments, Launch.WorkingDirectory,
			[WeakSelf, ExpectedGeneration, LaunchId](const FString& Line)
			{
				const TSharedPtr<FChatSession> This = WeakSelf.Pin();
				if (This && This->Generation == ExpectedGeneration && This->CliLaunchId == LaunchId)
				{
					This->HandleCodexLine(Line);
				}
			},
			[WeakSelf, ExpectedGeneration, LaunchId](int32 ReturnCode)
			{
				const TSharedPtr<FChatSession> This = WeakSelf.Pin();
				if (This && This->Generation == ExpectedGeneration && This->CliLaunchId == LaunchId)
				{
					This->HandleCliExit(ReturnCode);
				}
			},
			Error);
		if (!CliProcess)
		{
			Fail(Error);
		}
	}

	void FChatSession::HandleCodexLine(const FString& Line)
	{
		using namespace SessionPrivate;

		TSharedPtr<FJsonObject> Message;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
		if (!Line.StartsWith(TEXT("{")) || !FJsonSerializer::Deserialize(Reader, Message) || !Message.IsValid())
		{
			UE_LOG(LogAgentMcpChat, Log, TEXT("Codex: %s"), *Line);
			if (CliOtherOutput.Len() < 2000 && !Line.Contains(TEXT("Reading additional input from stdin")))
			{
				CliOtherOutput += StripTerminalCodes(Line) + TEXT("\n");
			}
			return;
		}

		FString Type;
		Message->TryGetStringField(TEXT("type"), Type);
		const FString Speaker = GetDefault<UAgentMcpChatSettings>()->CodexModel.IsEmpty()
			? FString(TEXT("ChatGPT")) : TEXT("ChatGPT · ") + GetDefault<UAgentMcpChatSettings>()->CodexModel;

		if (Type == TEXT("thread.started"))
		{
			FString ThreadId;
			if (Message->TryGetStringField(TEXT("thread_id"), ThreadId) && !ThreadId.IsEmpty())
			{
				CodexThreadId = ThreadId;
			}
			return;
		}

		if (Type == TEXT("item.started") || Type == TEXT("item.completed"))
		{
			const TSharedPtr<FJsonObject>* Item = nullptr;
			if (!Message->TryGetObjectField(TEXT("item"), Item))
			{
				return;
			}
			FString ItemType;
			(*Item)->TryGetStringField(TEXT("type"), ItemType);
			const bool bCompleted = Type == TEXT("item.completed");

			if (ItemType == TEXT("agent_message") && bCompleted)
			{
				FString Text;
				(*Item)->TryGetStringField(TEXT("text"), Text);
				Text.TrimStartAndEndInline();
				if (!Text.IsEmpty())
				{
					Emit(EChatEntryKind::Assistant, Speaker, Text);
					bCodexAnswered = true;
				}
				return;
			}

			if (ItemType == TEXT("mcp_tool_call"))
			{
				FToolCall Call;
				(*Item)->TryGetStringField(TEXT("tool"), Call.Name);
				const TSharedPtr<FJsonObject>* Arguments = nullptr;
				Call.Arguments = (*Item)->TryGetObjectField(TEXT("arguments"), Arguments) ? *Arguments : MakeShared<FJsonObject>();
				if (!bCompleted)
				{
					SetActivity(FString::Printf(TEXT("%s 실행 중..."), *Call.Name));
					return;
				}
				Emit(EChatEntryKind::Tool, TEXT("도구"), DescribeCall(Call));

				FString Status;
				(*Item)->TryGetStringField(TEXT("status"), Status);
				const TSharedPtr<FJsonObject>* ToolError = nullptr;
				FString ErrorMessage;
				if ((*Item)->TryGetObjectField(TEXT("error"), ToolError))
				{
					(*ToolError)->TryGetStringField(TEXT("message"), ErrorMessage);
				}
				if (Status == TEXT("failed") || !ErrorMessage.IsEmpty())
				{
					Emit(EChatEntryKind::Tool, TEXT("도구"), TEXT("도구 실패: ") + (ErrorMessage.IsEmpty() ? Status : ErrorMessage).Left(300));
				}
				SetActivity(TEXT("ChatGPT 응답을 기다리는 중..."));
				return;
			}

			if (ItemType == TEXT("error") && bCompleted)
			{
				FString Text;
				(*Item)->TryGetStringField(TEXT("message"), Text);
				UE_LOG(LogAgentMcpChat, Log, TEXT("Codex notice: %s"), *Text);
				return;
			}

			// Reasoning summaries and other items: shown among the steps, not as the answer.
			if (bCompleted && ItemType != TEXT("reasoning"))
			{
				Emit(EChatEntryKind::Status, FString(), FString::Printf(TEXT("(%s)"), *ItemType));
			}
			return;
		}

		if (Type == TEXT("turn.completed"))
		{
			bCliGotResult = true;
			Finish();
			return;
		}

		if (Type == TEXT("turn.failed") || Type == TEXT("error"))
		{
			bCliGotResult = true;
			FString Text;
			const TSharedPtr<FJsonObject>* ErrorObject = nullptr;
			if (Message->TryGetObjectField(TEXT("error"), ErrorObject))
			{
				(*ErrorObject)->TryGetStringField(TEXT("message"), Text);
			}
			if (Text.IsEmpty())
			{
				Message->TryGetStringField(TEXT("message"), Text);
			}
			if (Text.Contains(TEXT("login")) || Text.Contains(TEXT("401")) || Text.Contains(TEXT("auth")))
			{
				Text += TEXT(" 패널의 '계정'에서 ChatGPT에 로그인하세요.");
			}
			Fail(TEXT("ChatGPT(Codex) 오류: ") + (Text.IsEmpty() ? Type : Text));
		}
	}

	void FChatSession::SetUpCodex()
	{
		if (bBusy)
		{
			return;
		}
		++Generation;
		bBusy = true;

		FString Error;
		const FString Executable = FindCodexExecutable(Error);
		if (Executable.IsEmpty())
		{
			InstallCodex();
			return;
		}
		CheckCodexLogin(Executable, /*bSignInIfNeeded=*/true);
	}

	void FChatSession::InstallCodex()
	{
		FString Executable;
		FString Arguments;
		if (!GetCodexInstallCommand(Executable, Arguments))
		{
			Fail(TEXT("이 PC에서는 Codex CLI를 자동으로 설치할 수 없습니다. 직접 설치한 뒤 다시 눌러 주세요."));
			return;
		}

		Emit(EChatEntryKind::Status, FString(), TEXT("Codex CLI(ChatGPT)를 설치합니다. 1~2분 걸릴 수 있습니다."));
		SetActivity(TEXT("Codex CLI 설치 중..."));
		RunSetupProcess(Executable, Arguments, /*bShowUrls=*/false, [this](int32 ReturnCode, const FString& Output)
		{
			FString Error;
			const FString Installed = FindCodexExecutable(Error);
			if (Installed.IsEmpty())
			{
				Fail(FString::Printf(TEXT("Codex CLI를 설치하지 못했습니다(종료 코드 %d). %s"), ReturnCode, *Output.Right(600)));
				return;
			}
			Emit(EChatEntryKind::Status, FString(), TEXT("Codex CLI를 설치했습니다."));
			CheckCodexLogin(Installed, /*bSignInIfNeeded=*/true);
		});
	}

	void FChatSession::CheckCodexLogin(const FString& Executable, bool bSignInIfNeeded)
	{
		SetActivity(TEXT("ChatGPT 로그인 상태를 확인하는 중..."));
		RunSetupProcess(Executable, TEXT("login status"), /*bShowUrls=*/false,
			[this, Executable, bSignInIfNeeded](int32 ReturnCode, const FString& Output)
			{
				if (ReturnCode == 0 && Output.Contains(TEXT("Logged in")))
				{
					Emit(EChatEntryKind::Status, FString(), FString::Printf(TEXT("ChatGPT(Codex)에 로그인되어 있습니다(%s). 이제 질문하면 됩니다."),
						*Output.TrimStartAndEnd().Left(80)));
					Finish();
					return;
				}
				if (!bSignInIfNeeded)
				{
					Fail(TEXT("ChatGPT 로그인이 끝나지 않았습니다. '계정'에서 ChatGPT의 '로그인'을 다시 눌러 주세요."));
					return;
				}
				Emit(EChatEntryKind::Status, FString(),
					TEXT("브라우저에서 ChatGPT 로그인 페이지가 열립니다. ChatGPT 계정으로 로그인하면 자동으로 이어집니다."));
				SetActivity(TEXT("브라우저에서 로그인을 기다리는 중... (취소하려면 중지)"));
				RunSetupProcess(Executable, TEXT("login"), /*bShowUrls=*/true, [this, Executable](int32, const FString&)
				{
					CheckCodexLogin(Executable, /*bSignInIfNeeded=*/false);
				});
			});
	}

	FString FChatSession::GetCliModelLabel(const FString& Id) const
	{
		if (Id.IsEmpty())
		{
			return TEXT("기본 (계정 설정)");
		}
		const FChatModel* Model = FindCliModel(Id);
		return Model ? Model->Label : Id;
	}

	const FChatModel* FChatSession::FindCliModel(const FString& Id) const
	{
		if (GetDefault<UAgentMcpChatSettings>()->Provider == EAgentMcpChatProvider::Codex)
		{
			return CodexModels.FindByPredicate([&Id](const FChatModel& Model) { return Model.Id == Id; });
		}
		// A saved id may still carry its level (gemini-3.8-flash-high), so compare the base ids too.
		const FString Base = GetAntigravityBaseModel(Id);
		return AntigravityModels.FindByPredicate([&Id, &Base](const FChatModel& Model)
		{
			return Model.Id == Id || GetAntigravityBaseModel(Model.Id) == Base;
		});
	}

	FString FChatSession::MakeSystemPrompt(bool bHasOwnTools) const
	{
		FString Prompt = MakeBasePrompt();
		Prompt += TEXT(
			"\nTo read the project's C++ sources and config files, use source_find, source_search and source_read. Read the headers and "
			"sources when a question is about code or about how a Blueprint's parent class works, instead of telling the user to look. "
			"They decode CP949 files, so Korean comments read correctly.\n");
		Prompt += MakeModePrompt();
		if (bHasOwnTools)
		{
			// Antigravity brings shell, browser and file tools of its own; any of them is denied here, and a denial ends the answer.
			Prompt += TEXT(
				"\nUse only the tools of the agentmcp-chat MCP server (call_mcp_tool). Do not use run_command, the browser, web search, "
				"view_file, list_dir, grep_search or any file editing tool: they are blocked in this environment, and calling one ends "
				"your answer without a reply.\n");
		}
		return Prompt;
	}

	FString FChatSession::MakeModePrompt() const
	{
		const EAgentMcpChatMode Mode = GetDefault<UAgentMcpChatSettings>()->Mode;
		const bool bAssets = Mode == EAgentMcpChatMode::AssetEdit || Mode == EAgentMcpChatMode::AssetAndCodeEdit;
		const bool bCode = Mode == EAgentMcpChatMode::CodeEdit || Mode == EAgentMcpChatMode::AssetAndCodeEdit;
		const FString Navigation = TEXT(
			"\nTo show the user what you found, open assets in their editors (editor_open_assets), show them in the Content Browser "
			"(editor_show_in_content_browser), select and frame actors (actor_select) or move the viewport camera; these change nothing and "
			"need no approval. Offer it when it helps, for example after finding a Blueprint. Opening another level (level_open) replaces "
			"the open one, so it waits for the user's approval.\n");
		if (!bAssets && !bCode)
		{
			return Navigation + TEXT(
				"\nThe chat is in read-only mode: no tool here can change the project. When asked for a change, explain where and how to "
				"make it in the editor, and mention that the chat's permission mode can be switched to let you do it.\n");
		}

		FString Prompt = Navigation + TEXT(
			"\nEvery change you make waits for the user's approval in the chat panel, which shows the tool and its arguments. Make one "
			"focused change per call so each approval is easy to judge, and say what you are about to change before you call the tool. "
			"If a call is denied, do not retry it: ask the user what to do instead.\n");
		if (bAssets)
		{
			Prompt += TEXT(
				"You may change levels, actors, Blueprints, widgets, DataTables, string tables and other assets with the editing tools. "
				"Those changes are undoable with editor_undo, and nothing is written to disk until asset_save or level_save, which also "
				"need approval: save only when the user asks you to.\n");
		}
		if (bCode)
		{
			Prompt += TEXT(
				"You may edit text files in the source folders: read the file first, then prefer source_replace with a small exact edit "
				"over rewriting the file with source_write. File edits are not undoable in the editor; source control reverts them. After a "
				"C++ change, livecoding_compile applies changes inside function bodies; a change to a header, UPROPERTY, UFUNCTION or "
				"module layout needs the editor closed and a full build, so tell the user instead of compiling.\n");
		}
		else
		{
			Prompt += TEXT("No tool here can edit a source file.\n");
		}
		if (!bAssets)
		{
			Prompt += TEXT("No tool here can change an asset or level.\n");
		}
		return Prompt;
	}

	FString FChatSession::MakeBasePrompt() const
	{
		return FString::Printf(TEXT(
			"You are an assistant inside the Unreal Editor of the project '%s' (Unreal Engine %s). The people you talk to make this game: "
			"designers, artists and programmers, many of whom do not read code.\n"
			"\n"
			"Answer in the language the user writes in.\n"
			"Use the tools to look things up in the open editor instead of guessing: assets, levels and actors, Blueprints, widgets, "
			"DataTables, string tables and the editor log. Search before you say something does not exist.\n"
			"Name assets by their path (for example /Game/UI/WBP_Inventory) so people can find them in the Content Browser.\n"
			"Keep answers short and concrete, and say when a result was cut off or a search found nothing."),
			FApp::GetProjectName(), *FEngineVersion::Current().ToString(EVersionComponent::Patch));
	}
}
