#pragma once

#include "AgentMcpChatSettings.h"
#include "AgentMcpChatTypes.h"

#include "Interfaces/IHttpRequest.h"

namespace UE::AgentMcp::Chat
{
	/**
	 * MCP client of this editor's own endpoint. Only the tools the permission mode allows are offered to the model, and a call to any
	 * other name is refused here, before it reaches the server. Its requests carry the chat's client tag, so the server asks the
	 * person before anything changes.
	 */
	class FMcpClient : public TSharedFromThis<FMcpClient>
	{
	public:
		using FOnConnected = TFunction<void(bool bOk, const FString& Error)>;
		using FOnToolDone = TFunction<void(const FString& Text, bool bIsError)>;

		/** initialize, notifications/initialized, then tools/list, keeping the tools Mode allows. */
		void Connect(EAgentMcpChatMode InMode, FOnConnected OnDone);
		void CallTool(const FString& Name, const TSharedRef<FJsonObject>& Arguments, FOnToolDone OnDone);
		void CancelAll();

		bool IsConnected() const { return bConnected; }
		EAgentMcpChatMode GetMode() const { return Mode; }
		const FString& GetEndpoint() const { return Endpoint; }
		const TArray<FToolSpec>& GetTools() const { return Tools; }
		/** The tools left out by the permission mode, for clients that must deny or hide them by name. */
		const TArray<FString>& GetBlockedToolNames() const { return BlockedToolNames; }
		bool HasTool(const FString& Name) const;

	private:
		using FOnRpcDone = TFunction<void(const TSharedPtr<FJsonObject>& Result, const FString& Error, int32 ResponseCode)>;

		void SendRpc(const FString& Method, const TSharedPtr<FJsonObject>& Params, bool bNotification, float TimeoutSeconds, FOnRpcDone OnDone);
		void ListTools(FOnConnected OnDone);

		FString Endpoint;
		FString AuthToken;
		FString SessionId;
		int32 NextRequestId = 1;
		bool bConnected = false;
		EAgentMcpChatMode Mode = EAgentMcpChatMode::ReadOnly;
		TArray<FToolSpec> Tools;
		TArray<FString> BlockedToolNames;
		TArray<FHttpRequestPtr> InFlight;
	};
}
