#pragma once

#include "AgentMcpChatAntigravity.h"
#include "AgentMcpChatProviders.h"

namespace UE::AgentMcp::Chat
{
	class FCliProcess;
	class FMcpClient;
	struct FChatSessionState;

	enum class EChatEntryKind : uint8
	{
		User,
		Assistant,
		Tool,
		Status,
		Error,
	};

	struct FChatEntry
	{
		EChatEntryKind Kind = EChatEntryKind::Status;
		/** Who spoke, for example You or Claude. */
		FString Speaker;
		FString Text;
	};

	/**
	 * One conversation: sends the question to the model, runs the tool calls it asks for through MCP, and repeats until the model
	 * answers without tool calls. Game thread only.
	 */
	class FChatSession : public TSharedFromThis<FChatSession>
	{
	public:
		FChatSession();
		~FChatSession();

		TFunction<void(const FChatEntry&)> OnEntry;
		TFunction<void()> OnStateChanged;

		/** Returns false, after reporting why, when the question could not be sent. */
		bool Send(const FString& Text);
		void Cancel();
		/** Forgets the conversation. The next question also reconnects to MCP, picking up tools added since. */
		void Reset();

		bool IsBusy() const { return bBusy; }
		/** What the session is waiting for, for the status line. */
		const FString& GetActivity() const { return Activity; }

		void FetchModels(TFunction<void(const TArray<FString>& Models, const FString& Error)> OnDone);

		/**
		 * Gets Claude Code ready without a terminal: installs it when it is missing, then signs in through the browser when it is not
		 * signed in. Progress is reported as status entries.
		 */
		void SetUpClaudeCode();

		/** The same for the Antigravity CLI (Gemini): install, add the MCP permission rule, then sign in with Google. */
		void SetUpAntigravity();

		/** The same for the Codex CLI (ChatGPT): install, then sign in with the ChatGPT account in the browser. */
		void SetUpCodex();

		/** A setup step waits for text from the person, such as the sign-in code Google shows in the browser. */
		bool IsAwaitingInput() const { return bAwaitingInput; }
		/** Sends that text to the waiting step. Returns false when nothing waits for input. */
		bool SubmitInput(const FString& Text);

		/** What a saved conversation needs to go on later: the provider and its conversation id or API history. */
		FChatSessionState CaptureState() const;
		/** Continues a saved conversation: forgets the current one, switches the provider setting and takes over the ids or history. */
		void RestoreState(const FChatSessionState& State);

		/** The label the CLI (agy models, codex debug models) gave a model id, for the model picker. */
		FString GetCliModelLabel(const FString& Id) const;
		/** The model of the current CLI provider an id belongs to, or null before its model list has been read. */
		const FChatModel* FindCliModel(const FString& Id) const;

	private:
		void Emit(EChatEntryKind Kind, const FString& Speaker, const FString& Text);
		void SetActivity(const FString& InActivity);
		bool PrepareProvider(FString& OutError);
		void EnsureConnected(TFunction<void()> OnReady);
		void RequestTurn();
		void HandleTurn(int32 ResponseCode, const FString& Body);
		void RunToolCall(TSharedRef<TArray<FToolCall>> Calls, int32 Index, TSharedRef<TArray<FToolOutcome>> Outcomes);
		/** Ends the answer with an error and drops the unanswered question from the history. */
		void Fail(const FString& Error);
		void Finish();
		/** bHasOwnTools: the client brings built-in tools that must not be used (Antigravity). */
		FString MakeSystemPrompt(bool bHasOwnTools) const;
		FString MakeBasePrompt() const;
		/** What the permission mode lets the model change, and how approvals work. */
		FString MakeModePrompt() const;

		/** Claude Code runs the tool loop itself: one claude -p process per question, continued with --resume. */
		void LaunchClaudeCode(const FString& Question);
		void HandleClaudeCodeLine(const FString& Line);
		void HandleCliExit(int32 ReturnCode);

		void InstallClaudeCode();
		/** Runs claude auth status; signs in when bSignInIfNeeded and the user is signed out. */
		void CheckClaudeCodeLogin(const FString& Executable, bool bSignInIfNeeded);
		void SignInClaudeCode(const FString& Executable);
		/**
		 * Runs a setup step with its output collected, and calls OnExit with that output on the game thread. bWithInput gives the step
		 * a stdin for SubmitInput; OnLine sees each line as it arrives.
		 */
		void RunSetupProcess(const FString& Executable, const FString& Arguments, bool bShowUrls, TFunction<void(int32 ReturnCode, const FString& Output)> OnExit,
			bool bWithInput = false, TFunction<void(const FString& Line)> OnLine = nullptr);

		/** Antigravity runs the tool loop itself, like Claude Code: one agy -p process per question, continued with --conversation. */
		void StartAntigravityTurn(const FString& Question);
		void LaunchAntigravity(const FString& Prompt);
		void HandleAntigravityLine(const FString& Line);
		void InstallAntigravity();
		/** Runs a one-word print turn: it succeeds when signed in and starts Google's sign-in when not. */
		void CheckAntigravityLogin(const FString& Executable);

		/** Codex runs the tool loop itself too: codex exec per question, continued with codex exec resume. */
		void StartCodexTurn(const FString& Question);
		void LaunchCodex(const FString& Prompt);
		void HandleCodexLine(const FString& Line);
		void InstallCodex();
		/** Runs codex login status; signs in with codex login when bSignInIfNeeded and the user is signed out. */
		void CheckCodexLogin(const FString& Executable, bool bSignInIfNeeded);

		TUniquePtr<IChatProvider> Provider;
		TSharedPtr<FCliProcess, ESPMode::ThreadSafe> CliProcess;
		FString ClaudeCodeSessionId;
		/** Lines that were not stream-json, usually an error printed before the session started. */
		FString CliOtherOutput;
		bool bCliMode = false;
		/** The model Claude Code reported for this conversation, shown with its answers. */
		FString ClaudeCodeModelInUse;
		bool bCliGotResult = false;
		/** Incremented by every CLI launch, so callbacks of a replaced process do nothing. */
		uint32 CliLaunchId = 0;
		FString AntigravityConversationId;
		FString AntigravityModelInUse;
		/** Text of the answer steps still streaming, by step index. */
		TMap<int32, FString> AntigravityStepText;
		TArray<FChatModel> AntigravityModels;
		TArray<FChatModel> CodexModels;
		bool bAntigravityAnswered = false;
		/** Automatic retries of this question after Antigravity ended it on a denied action. */
		int32 AntigravityRetries = 0;
		bool bAwaitingInput = false;
		FString CodexThreadId;
		bool bCodexAnswered = false;
		/** A restored API history, handed to the provider when it is created for the next question. */
		TArray<TSharedPtr<FJsonValue>> PendingApiHistory;
		TSharedPtr<FCliProcess, ESPMode::ThreadSafe> ModelListProcess;
		TSharedPtr<FMcpClient> Mcp;
		FHttpRequestPtr PendingRequest;
		FHttpRequestPtr ModelListRequest;
		FString Activity;
		int32 SafeHistoryLength = 0;
		int32 Rounds = 0;
		/** Incremented by every Send and Cancel, so callbacks of a stopped answer do nothing. */
		uint32 Generation = 0;
		bool bBusy = false;
	};
}
