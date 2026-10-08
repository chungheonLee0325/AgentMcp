#include "AgentMcpChatMcpClient.h"

#include "AgentMcpChatPermissions.h"

#include "AgentMcpCompat.h"
#include "AgentMcpSettings.h"
#include "AgentMcpToolset.h"

#include "Dom/JsonValue.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UE::AgentMcp::Chat
{
	namespace McpClientPrivate
	{
		const TCHAR* const ProtocolVersion = TEXT("2025-06-18");
		const TCHAR* const SessionIdHeader = TEXT("Mcp-Session-Id");
		constexpr float HandshakeTimeoutSeconds = 30.0f;
		// Long enough for the person to read an approval card.
		constexpr float ToolTimeoutSeconds = 900.0f;
	}

	void FMcpClient::Connect(EAgentMcpChatMode InMode, FOnConnected OnDone)
	{
		using namespace McpClientPrivate;

		Mode = InMode;
		bConnected = false;
		SessionId.Reset();
		Tools.Reset();
		BlockedToolNames.Reset();

		const FAgentMcpRuntimeInfo Info = UE::AgentMcp::GetRuntimeInfo();
		if (!Info.bServerRunning || Info.EndpointUrl.IsEmpty())
		{
			OnDone(false, FString::Printf(TEXT("이 에디터의 Agent MCP 서버가 실행 중이 아닙니다.%s%s"),
				Info.LastError.IsEmpty() ? TEXT("") : TEXT(" "), *Info.LastError));
			return;
		}
		Endpoint = Info.EndpointUrl;
		AuthToken = GetDefault<UAgentMcpSettings>()->AuthToken;

		TSharedRef<FJsonObject> ClientInfo = MakeShared<FJsonObject>();
		ClientInfo->SetStringField(TEXT("name"), TEXT("agent-mcp-chat"));
		ClientInfo->SetStringField(TEXT("version"), TEXT("0.1.0"));
		TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("protocolVersion"), ProtocolVersion);
		Params->SetObjectField(TEXT("capabilities"), MakeShared<FJsonObject>());
		Params->SetObjectField(TEXT("clientInfo"), ClientInfo);

		TWeakPtr<FMcpClient> WeakSelf = AsShared();
		SendRpc(TEXT("initialize"), Params, /*bNotification=*/false, HandshakeTimeoutSeconds,
			[WeakSelf, OnDone](const TSharedPtr<FJsonObject>& Result, const FString& Error, int32 ResponseCode)
			{
				const TSharedPtr<FMcpClient> This = WeakSelf.Pin();
				if (!This)
				{
					return;
				}
				if (!Result.IsValid())
				{
					OnDone(false, TEXT("MCP 연결(initialize)에 실패했습니다: ") + Error);
					return;
				}
				This->SendRpc(TEXT("notifications/initialized"), nullptr, /*bNotification=*/true, HandshakeTimeoutSeconds,
					[](const TSharedPtr<FJsonObject>&, const FString&, int32) {});
				This->ListTools(OnDone);
			});
	}

	void FMcpClient::ListTools(FOnConnected OnDone)
	{
		TWeakPtr<FMcpClient> WeakSelf = AsShared();
		SendRpc(TEXT("tools/list"), MakeShared<FJsonObject>(), /*bNotification=*/false, McpClientPrivate::HandshakeTimeoutSeconds,
			[WeakSelf, OnDone](const TSharedPtr<FJsonObject>& Result, const FString& Error, int32 ResponseCode)
			{
				const TSharedPtr<FMcpClient> This = WeakSelf.Pin();
				if (!This)
				{
					return;
				}
				const TArray<TSharedPtr<FJsonValue>>* ToolArray = nullptr;
				if (!Result.IsValid() || !Result->TryGetArrayField(TEXT("tools"), ToolArray))
				{
					OnDone(false, TEXT("MCP 도구 목록(tools/list)을 가져오지 못했습니다: ") + Error);
					return;
				}

				for (const TSharedPtr<FJsonValue>& ToolValue : *ToolArray)
				{
					const TSharedPtr<FJsonObject>* ToolObject = nullptr;
					if (!ToolValue->TryGetObject(ToolObject))
					{
						continue;
					}
					const TSharedPtr<FJsonObject>* Annotations = nullptr;
					bool bReadOnly = false;
					if ((*ToolObject)->TryGetObjectField(TEXT("annotations"), Annotations))
					{
						(*Annotations)->TryGetBoolField(TEXT("readOnlyHint"), bReadOnly);
					}
					const FString Name = (*ToolObject)->GetStringField(TEXT("name"));
					if (!IsToolAllowedInMode(Name, bReadOnly, This->Mode))
					{
						This->BlockedToolNames.Add(Name);
						continue;
					}

					FToolSpec Spec;
					Spec.Name = Name;
					Spec.bReadOnly = bReadOnly;
					(*ToolObject)->TryGetStringField(TEXT("description"), Spec.Description);
					const TSharedPtr<FJsonObject>* Schema = nullptr;
					if ((*ToolObject)->TryGetObjectField(TEXT("inputSchema"), Schema))
					{
						Spec.InputSchema = *Schema;
					}
					This->Tools.Add(MoveTemp(Spec));
				}

				if (This->Tools.Num() == 0)
				{
					OnDone(false, TEXT("MCP 서버에 읽기 전용 도구가 없습니다. 채팅을 쓰려면 Agent MCP 설정의 Exposure Mode가 Native여야 합니다."));
					return;
				}
				This->bConnected = true;
				OnDone(true, FString());
			});
	}

	bool FMcpClient::HasTool(const FString& Name) const
	{
		return Tools.ContainsByPredicate([&Name](const FToolSpec& Tool) { return Tool.Name == Name; });
	}

	void FMcpClient::CallTool(const FString& Name, const TSharedRef<FJsonObject>& Arguments, FOnToolDone OnDone)
	{
		if (!HasTool(Name))
		{
			OnDone(FString::Printf(TEXT("이 채팅의 권한 모드에서는 '%s' 도구를 쓸 수 없습니다. 요청에 포함된 도구만 호출할 수 있습니다."), *Name), true);
			return;
		}

		TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("name"), Name);
		Params->SetObjectField(TEXT("arguments"), Arguments);

		TWeakPtr<FMcpClient> WeakSelf = AsShared();
		SendRpc(TEXT("tools/call"), Params, /*bNotification=*/false, McpClientPrivate::ToolTimeoutSeconds,
			[WeakSelf, OnDone](const TSharedPtr<FJsonObject>& Result, const FString& Error, int32 ResponseCode)
			{
				if (!Result.IsValid())
				{
					// An expired session answers with an HTTP error; connect again before the next question.
					if (const TSharedPtr<FMcpClient> This = WeakSelf.Pin(); This && ResponseCode != 200)
					{
						This->bConnected = false;
					}
					OnDone(Error, true);
					return;
				}

				TArray<FString> Parts;
				const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
				if (Result->TryGetArrayField(TEXT("content"), Content))
				{
					for (const TSharedPtr<FJsonValue>& ItemValue : *Content)
					{
						const TSharedPtr<FJsonObject>* Item = nullptr;
						if (!ItemValue->TryGetObject(Item))
						{
							continue;
						}
						const FString Type = (*Item)->GetStringField(TEXT("type"));
						if (Type == TEXT("text"))
						{
							Parts.Add((*Item)->GetStringField(TEXT("text")));
						}
						else
						{
							Parts.Add(FString::Printf(TEXT("[%s content is not shown in this chat]"), *Type));
						}
					}
				}
				bool bIsError = false;
				Result->TryGetBoolField(TEXT("isError"), bIsError);
				OnDone(FString::Join(Parts, TEXT("\n")), bIsError);
			});
	}

	void FMcpClient::CancelAll()
	{
		// CancelRequest runs the completion callbacks, which remove entries; iterate a copy.
		const TArray<FHttpRequestPtr> Requests = InFlight;
		InFlight.Reset();
		for (const FHttpRequestPtr& Request : Requests)
		{
			Request->CancelRequest();
		}
	}

	void FMcpClient::SendRpc(const FString& Method, const TSharedPtr<FJsonObject>& Params, bool bNotification, float TimeoutSeconds, FOnRpcDone OnDone)
	{
		using namespace McpClientPrivate;

		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
		Body->SetStringField(TEXT("method"), Method);
		if (!bNotification)
		{
			Body->SetNumberField(TEXT("id"), NextRequestId++);
		}
		if (Params.IsValid())
		{
			Body->SetObjectField(TEXT("params"), Params);
		}

		FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
		Request->SetVerb(TEXT("POST"));
		Request->SetURL(Endpoint);
		Request->SetTimeout(TimeoutSeconds);
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetHeader(TEXT("Accept"), TEXT("application/json, text/event-stream"));
		Request->SetHeader(TEXT("MCP-Protocol-Version"), ProtocolVersion);
		Request->SetHeader(TEXT("X-AgentMcp-Client"), ChatClientTag);
		if (!SessionId.IsEmpty())
		{
			Request->SetHeader(SessionIdHeader, SessionId);
		}
		if (!AuthToken.IsEmpty())
		{
			Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + AuthToken);
		}
		Request->SetContentAsString(UE::AgentMcp::Compat::JsonObjectToString(Body));

		TWeakPtr<FMcpClient> WeakSelf = AsShared();
		Request->OnProcessRequestComplete().BindLambda(
			[WeakSelf, OnDone](FHttpRequestPtr CompletedRequest, FHttpResponsePtr Response, bool bConnectedSuccessfully)
			{
				if (const TSharedPtr<FMcpClient> This = WeakSelf.Pin())
				{
					This->InFlight.Remove(CompletedRequest);
					if (Response.IsValid())
					{
						const FString NewSessionId = Response->GetHeader(SessionIdHeader);
						if (!NewSessionId.IsEmpty())
						{
							This->SessionId = NewSessionId;
						}
					}
				}

				if (!bConnectedSuccessfully || !Response.IsValid())
				{
					OnDone(nullptr, TEXT("에디터의 MCP 엔드포인트가 응답하지 않습니다."), 0);
					return;
				}

				const int32 ResponseCode = Response->GetResponseCode();
				TSharedPtr<FJsonObject> Reply;
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
				if (!FJsonSerializer::Deserialize(Reader, Reply) || !Reply.IsValid())
				{
					// A notification is answered with 202 and no body.
					OnDone(nullptr, FString::Printf(TEXT("HTTP %d: JSON-RPC 본문이 없습니다."), ResponseCode), ResponseCode);
					return;
				}

				const TSharedPtr<FJsonObject>* Error = nullptr;
				if (Reply->TryGetObjectField(TEXT("error"), Error))
				{
					FString Message;
					(*Error)->TryGetStringField(TEXT("message"), Message);
					OnDone(nullptr, Message.IsEmpty() ? TEXT("JSON-RPC 오류입니다.") : Message, ResponseCode);
					return;
				}

				const TSharedPtr<FJsonObject>* Result = nullptr;
				if (!Reply->TryGetObjectField(TEXT("result"), Result))
				{
					OnDone(nullptr, TEXT("JSON-RPC 응답에 result가 없습니다."), ResponseCode);
					return;
				}
				OnDone(*Result, FString(), ResponseCode);
			});

		InFlight.Add(Request);
		Request->ProcessRequest();
	}
}
