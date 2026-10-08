#pragma once

#include "AgentMcpChatSession.h"
#include "AgentMcpChatSettings.h"

#include "Dom/JsonValue.h"
#include "Misc/DateTime.h"

namespace UE::AgentMcp::Chat
{
	/** What a session needs to continue a conversation: the provider and its conversation id or history. */
	struct FChatSessionState
	{
		EAgentMcpChatProvider Provider = EAgentMcpChatProvider::ClaudeCode;
		FString ClaudeCodeSessionId;
		FString AntigravityConversationId;
		FString CodexThreadId;
		/** The API providers' history in their wire format. */
		TArray<TSharedPtr<FJsonValue>> ApiHistory;
	};

	/** One saved conversation: what the panel shows and what the session needs to go on. */
	struct FChatConversation
	{
		FString Id;
		FString Title;
		FDateTime CreatedAt;
		FDateTime UpdatedAt;
		FChatSessionState State;
		TArray<FChatEntry> Entries;
	};

	/**
	 * Conversations saved as JSON files under Saved/AgentMcpChat/Conversations of the project, so they stay on this PC and out of
	 * source control.
	 */
	namespace History
	{
		TSharedRef<FChatConversation> Create();
		/** Every saved conversation, newest first. Entries are loaded too; conversations are small. */
		TArray<TSharedRef<FChatConversation>> LoadAll();
		bool Save(const FChatConversation& Conversation);
		void Delete(const FString& Id);
	}
}
