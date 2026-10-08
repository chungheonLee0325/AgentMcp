#pragma once

#include "AgentMcpChatSettings.h"

#include "CoreMinimal.h"

namespace UE::AgentMcp::Chat
{
	class FCliProcess;

	enum class EChatConnectionState : uint8
	{
		Unknown,
		Checking,
		NotInstalled,
		SignedOut,
		SignedIn,
		/** API providers: a key is set (in the settings or the environment). */
		KeyReady,
		KeyMissing,
		Failed,
	};

	struct FConnectionStatus
	{
		EChatConnectionState State = EChatConnectionState::Unknown;
		/** The account or what went wrong, for the person. */
		FString Detail;
	};

	/**
	 * What the connection list shows: whether each CLI is installed and signed in, and whether the API providers have a key. Checks
	 * run in processes of their own, so they never get in the way of an answer. Game thread only.
	 */
	class FChatConnections : public TSharedFromThis<FChatConnections>
	{
	public:
		~FChatConnections();

		/** Called whenever a status changes. */
		TFunction<void()> OnChanged;

		void RefreshAll();
		void Refresh(EAgentMcpChatProvider Provider);

		/** Signs the CLI out (Claude Code, Codex). Antigravity has no sign-out command. */
		void SignOut(EAgentMcpChatProvider Provider);
		static bool CanSignOut(EAgentMcpChatProvider Provider);

		FConnectionStatus Get(EAgentMcpChatProvider Provider) const;

	private:
		void Set(EAgentMcpChatProvider Provider, EChatConnectionState State, const FString& Detail);
		/** Runs Executable with Arguments and hands its output to OnExit; ends it after TimeoutSeconds. */
		void Run(EAgentMcpChatProvider Provider, const FString& Executable, const FString& Arguments, const FString& WorkingDirectory,
			float TimeoutSeconds, TFunction<void(int32 ReturnCode, const FString& Output)> OnExit);

		TMap<EAgentMcpChatProvider, FConnectionStatus> Statuses;
		TMap<EAgentMcpChatProvider, TSharedPtr<FCliProcess, ESPMode::ThreadSafe>> Processes;
		TMap<EAgentMcpChatProvider, uint32> RunIds;
		uint32 NextRunId = 1;
	};
}
