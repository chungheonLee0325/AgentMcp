#pragma once

#include "AgentMcpChatSettings.h"
#include "AgentMcpChatTypes.h"

#include "Interfaces/IHttpRequest.h"
#include "Templates/UniquePtr.h"

namespace UE::AgentMcp::Chat
{
	struct FProviderConfig
	{
		EAgentMcpChatProvider Provider = EAgentMcpChatProvider::OpenAI;
		/** Without a trailing slash. */
		FString BaseUrl;
		FString ApiKey;
		FString Model;
		/** Claude Code only. Empty leaves the model default. */
		FString Effort;
		float TimeoutSeconds = 300.0f;
	};

	/** Fills Out from the chat settings. Returns false with a message for the user when a key or model is missing. */
	bool ReadProviderConfig(EAgentMcpChatProvider Provider, FProviderConfig& Out, FString& OutError);

	/** Short name for chat labels, such as Claude or Gemini. */
	FString GetProviderLabel(EAgentMcpChatProvider Provider);

	/**
	 * One model API. The provider keeps the conversation in its own wire format, because each API has its own shape for tool calls
	 * and results, and some (Claude thinking blocks, Gemini thought signatures) must be echoed back unchanged.
	 */
	class IChatProvider
	{
	public:
		virtual ~IChatProvider() = default;

		virtual void SetConfig(const FProviderConfig& InConfig) = 0;
		virtual const FProviderConfig& GetConfig() const = 0;

		virtual void AddUserText(const FString& Text) = 0;
		/** Adds the results of every tool call of the last reply, in one turn. */
		virtual void AddToolOutcomes(const TArray<FToolOutcome>& Outcomes) = 0;

		virtual int32 GetHistoryLength() const = 0;
		/** The conversation in the API's wire format, for saving and restoring it. */
		virtual const TArray<TSharedPtr<FJsonValue>>& GetHistory() const = 0;
		virtual void SetHistory(const TArray<TSharedPtr<FJsonValue>>& InHistory) = 0;
		/** Drops turns after Length, to return to a consistent history after a failed or stopped answer. */
		virtual void TruncateHistory(int32 Length) = 0;

		virtual FHttpRequestRef MakeTurnRequest(const FString& SystemPrompt, const TArray<FToolSpec>& Tools) const = 0;
		/** Parses a turn response and appends the assistant turn to the history. */
		virtual FModelReply ParseTurnResponse(int32 ResponseCode, const FString& Body) = 0;

		virtual FHttpRequestRef MakeModelListRequest() const = 0;
		virtual TArray<FString> ParseModelList(int32 ResponseCode, const FString& Body, FString& OutError) const = 0;
	};

	TUniquePtr<IChatProvider> MakeProvider(const FProviderConfig& Config);
}
