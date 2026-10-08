#pragma once

#include "AgentMcpChatSettings.h"
#include "AgentMcpToolset.h"

namespace UE::AgentMcp::Chat
{
	/** X-AgentMcp-Client value the chat's MCP connections send, so the server routes their changes through the approval gate. */
	extern const TCHAR* const ChatClientTag;

	/** Whether a tool may be offered in a mode. Read-only tools always may; some tools never may (client config, play sessions). */
	bool IsToolAllowedInMode(const FString& ToolName, bool bReadOnly, EAgentMcpChatMode Mode);

	FString GetModeLabel(EAgentMcpChatMode Mode);

	/** Tools that only move the person's view (open an asset editor, show in the Content Browser, select, frame). Never asked. */
	bool IsNavigationTool(const FString& ToolName);

	/** A call waiting in the panel for the person's decision. */
	struct FPendingApproval
	{
		FAgentMcpApprovalRequest Request;
		FAgentMcpApprovalDecision Decide;
		bool bResolved = false;

		void Resolve(bool bApproved, const FString& Reason = FString());
	};

	/**
	 * The approval gate of the chat's client tag. It refuses tools outside the current mode on the server side, whatever the client
	 * offered the model, and hands the rest to the open chat panel. Game thread only.
	 */
	class FChatApprovals
	{
	public:
		static FChatApprovals& Get();

		void Register();
		void Unregister();

		/** The panel's handler for new requests; unset when no panel is open, and then every request is refused. */
		TFunction<void(const TSharedRef<FPendingApproval>&)> OnRequest;

		/** Approves the rest of the current question without asking. */
		void ApproveAllForQuestion() { bApproveAll = true; }
		/** Called when a new question starts. */
		void BeginQuestion() { bApproveAll = false; }

		/** Refuses every pending request, for example when the answer is stopped or the panel closes. */
		void DenyAll(const FString& Reason);

	private:
		void HandleRequest(const FAgentMcpApprovalRequest& Request, FAgentMcpApprovalDecision Decide);

		TArray<TSharedRef<FPendingApproval>> Pending;
		bool bApproveAll = false;
	};
}
