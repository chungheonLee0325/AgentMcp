#include "AgentMcpServer.h"

#include "AgentMcpCompat.h"
#include "AgentMcpProtocol.h"
#include "IAgentMcpTool.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformTime.h"
#include "HttpPath.h"
#include "HttpRequestHandler.h"
#include "HttpResultCallback.h"
#include "HttpRouteHandle.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Misc/Guid.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include <type_traits>

namespace UE::AgentMcp::ServerPrivate
{
	constexpr int32 JsonRpcParseError = -32700;
	constexpr int32 JsonRpcInvalidRequest = -32600;
	constexpr int32 JsonRpcMethodNotFound = -32601;
	constexpr int32 JsonRpcInvalidParams = -32602;
	constexpr int32 JsonRpcRequestCancelled = -32800;

	constexpr double SessionIdleTimeoutSeconds = 4.0 * 60.0 * 60.0;

	const TCHAR* const SessionIdHeader = TEXT("Mcp-Session-Id");
	const TCHAR* const ProtocolVersionHeader = TEXT("MCP-Protocol-Version");

	FString GetHeader(const FHttpServerRequest& Request, const TCHAR* Name)
	{
		if (const TArray<FString>* Values = Request.Headers.Find(Name))
		{
			if (Values->Num() > 0)
			{
				return (*Values)[0].TrimStartAndEnd();
			}
		}
		return FString();
	}

	/**
	 * DNS rebinding protection: browsers send an Origin header, and only pages served from this machine may call the server.
	 * Clients that are not browsers send no Origin header and are allowed.
	 */
	bool IsOriginAllowed(const FHttpServerRequest& Request)
	{
		const FString Origin = GetHeader(Request, TEXT("Origin"));
		if (Origin.IsEmpty())
		{
			return true;
		}

		// An origin is scheme "://" host [":" port], and host names are case-insensitive.
		const int32 SchemeSeparator = Origin.Find(TEXT("://"), ESearchCase::CaseSensitive);
		if (SchemeSeparator == INDEX_NONE)
		{
			return false;
		}
		const FString Authority = Origin.RightChop(SchemeSeparator + 3).ToLower();

		// A bracketed IPv6 literal keeps its brackets; any other host ends at the port separator.
		int32 HostLength = Authority.Len();
		if (Authority.StartsWith(TEXT("[")))
		{
			const int32 ClosingBracket = Authority.Find(TEXT("]"), ESearchCase::CaseSensitive);
			if (ClosingBracket == INDEX_NONE)
			{
				return false;
			}
			HostLength = ClosingBracket + 1;
		}
		else if (const int32 PortSeparator = Authority.Find(TEXT(":"), ESearchCase::CaseSensitive); PortSeparator != INDEX_NONE)
		{
			HostLength = PortSeparator;
		}
		const FString Host = Authority.Left(HostLength);

		static const TCHAR* const LoopbackHosts[] = { TEXT("localhost"), TEXT("127.0.0.1"), TEXT("[::1]") };
		for (const TCHAR* LoopbackHost : LoopbackHosts)
		{
			if (Host.Equals(LoopbackHost, ESearchCase::CaseSensitive))
			{
				return true;
			}
		}
		return false;
	}

	FString RequestIdToKey(const TSharedPtr<FJsonValue>& Id)
	{
		if (!Id.IsValid())
		{
			return TEXT("none");
		}

		switch (Id->Type)
		{
		case EJson::String:
			return TEXT("s:") + Id->AsString();
		case EJson::Number:
			return FString::Printf(TEXT("n:%.17g"), Id->AsNumber());
		default:
			return TEXT("other");
		}
	}

	TSharedRef<FJsonObject> MakeJsonRpcEnvelope(const TSharedPtr<FJsonValue>& Id)
	{
		TSharedRef<FJsonObject> Envelope = MakeShared<FJsonObject>();
		Envelope->SetStringField(TEXT("jsonrpc"), UE::AgentMcp::JsonRpcVersion);

		TSharedPtr<FJsonValue> IdValue = Id;
		if (!IdValue.IsValid())
		{
			IdValue = MakeShared<FJsonValueNull>();
		}
		Envelope->SetField(TEXT("id"), IdValue);
		return Envelope;
	}

	TSharedRef<FJsonObject> MakeJsonRpcResult(const TSharedPtr<FJsonValue>& Id, const TSharedRef<FJsonObject>& Result)
	{
		TSharedRef<FJsonObject> Envelope = MakeJsonRpcEnvelope(Id);
		Envelope->SetObjectField(TEXT("result"), Result);
		return Envelope;
	}

	TSharedRef<FJsonObject> MakeJsonRpcError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message)
	{
		TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetNumberField(TEXT("code"), Code);
		Error->SetStringField(TEXT("message"), Message);

		TSharedRef<FJsonObject> Envelope = MakeJsonRpcEnvelope(Id);
		Envelope->SetObjectField(TEXT("error"), Error);
		return Envelope;
	}

	TUniquePtr<FHttpServerResponse> MakeJsonResponse(const TSharedRef<FJsonObject>& Body, EHttpServerResponseCodes Code, const FString& SessionId)
	{
		TUniquePtr<FHttpServerResponse> Response =
			FHttpServerResponse::Create(UE::AgentMcp::Compat::JsonObjectToUtf8(Body), TEXT("application/json"));
		Response->Code = Code;
		if (!SessionId.IsEmpty())
		{
			Response->Headers.Add(SessionIdHeader, { SessionId });
		}
		return Response;
	}

	TUniquePtr<FHttpServerResponse> MakeEmptyResponse(EHttpServerResponseCodes Code, const FString& SessionId)
	{
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(FString(), TEXT("application/json"));
		Response->Code = Code;
		if (!SessionId.IsEmpty())
		{
			Response->Headers.Add(SessionIdHeader, { SessionId });
		}
		return Response;
	}

	EHttpServerRequestVerbs CombineVerbs(std::initializer_list<EHttpServerRequestVerbs> Verbs)
	{
		using FUnderlying = std::underlying_type_t<EHttpServerRequestVerbs>;
		FUnderlying Combined = 0;
		for (EHttpServerRequestVerbs Verb : Verbs)
		{
			Combined |= static_cast<FUnderlying>(Verb);
		}
		return static_cast<EHttpServerRequestVerbs>(Combined);
	}
}

struct FAgentMcpSession
{
	FString Id;
	FString ProtocolVersion;
	FString ClientName;
	FString ClientVersion;
	bool bInitialized = false;
	double LastSeenSeconds = 0.0;

	/** Request key -> cancel flag for in-flight tools/call requests. */
	TMap<FString, TSharedRef<bool>> ActiveRequests;
};

class FAgentMcpServerImpl : public TSharedFromThis<FAgentMcpServerImpl>
{
public:
	FAgentMcpServerConfig Config;
	TSharedPtr<IHttpRouter> Router;
	FHttpRouteHandle RouteHandle;
	bool bRunning = false;

	TArray<TSharedRef<IAgentMcpTool>> Tools;
	TMap<FString, TSharedRef<IAgentMcpTool>> ToolsByName;
	TMap<FString, TSharedRef<FAgentMcpSession>> Sessions;

	bool Start(const FAgentMcpServerConfig& InConfig, FString& OutError);
	void Stop();
	void SetTools(const TArray<TSharedRef<IAgentMcpTool>>& InTools);

private:
	bool HandleRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandlePost(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleDelete(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleMethodNotAllowed(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	void HandleInitialize(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params, const FHttpResultCallback& OnComplete);
	void HandleToolsList(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params, const FString& SessionId, const FHttpResultCallback& OnComplete);
	void HandleToolsCall(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params, const TSharedRef<FAgentMcpSession>& Session, const FHttpResultCallback& OnComplete);

	bool IsAuthorized(const FHttpServerRequest& Request) const;
	void EvictSessions();
};

bool FAgentMcpServerImpl::Start(const FAgentMcpServerConfig& InConfig, FString& OutError)
{
	using namespace UE::AgentMcp::ServerPrivate;

	Stop();

	Config = InConfig;
	if (!Config.UrlPath.StartsWith(TEXT("/")))
	{
		Config.UrlPath = TEXT("/") + Config.UrlPath;
	}

	FHttpServerModule& HttpServerModule = FHttpServerModule::Get();
	// GetHttpRouter reports a bind failure only while listeners are enabled (UE 5.5 FHttpServerModule::GetHttpRouter). In a fresh
	// editor no module has enabled them yet, and a port already used by another process then went unnoticed.
	HttpServerModule.StartAllListeners();
	Router = HttpServerModule.GetHttpRouter(Config.Port, /*bFailOnBindFailure=*/true);
	if (!Router.IsValid())
	{
		OutError = FString::Printf(TEXT("Could not bind an HTTP listener on port %u. Another process may be using it."), Config.Port);
		return false;
	}

	// One route for all verbs; the verb is dispatched in HandleRequest.
	TWeakPtr<FAgentMcpServerImpl> WeakSelf = AsShared();
	RouteHandle = Router->BindRoute(
		FHttpPath(Config.UrlPath),
		CombineVerbs({ EHttpServerRequestVerbs::VERB_POST, EHttpServerRequestVerbs::VERB_GET, EHttpServerRequestVerbs::VERB_DELETE }),
		FHttpRequestHandler::CreateLambda([WeakSelf](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (TSharedPtr<FAgentMcpServerImpl> Pinned = WeakSelf.Pin())
			{
				return Pinned->HandleRequest(Request, OnComplete);
			}
			return false;
		}));

	if (!RouteHandle.IsValid())
	{
		OutError = FString::Printf(TEXT("Route %s on port %u could not be bound (already in use?)."), *Config.UrlPath, Config.Port);
		Router.Reset();
		return false;
	}

	bRunning = true;
	return true;
}

void FAgentMcpServerImpl::Stop()
{
	if (Router.IsValid() && RouteHandle.IsValid())
	{
		Router->UnbindRoute(RouteHandle);
	}
	RouteHandle.Reset();
	Router.Reset();

	for (TPair<FString, TSharedRef<FAgentMcpSession>>& SessionPair : Sessions)
	{
		for (TPair<FString, TSharedRef<bool>>& RequestPair : SessionPair.Value->ActiveRequests)
		{
			*RequestPair.Value = true;
		}
	}
	Sessions.Reset();
	bRunning = false;
}

void FAgentMcpServerImpl::SetTools(const TArray<TSharedRef<IAgentMcpTool>>& InTools)
{
	Tools.Reset();
	ToolsByName.Reset();

	for (const TSharedRef<IAgentMcpTool>& Tool : InTools)
	{
		const FString Name = Tool->GetName();
		if (!UE::AgentMcp::IsValidMcpToolName(Name))
		{
			UE_LOG(LogAgentMcpProtocol, Error, TEXT("Skipping MCP tool with invalid name '%s'."), *Name);
			continue;
		}
		if (ToolsByName.Contains(Name))
		{
			UE_LOG(LogAgentMcpProtocol, Error, TEXT("Skipping duplicate MCP tool '%s'."), *Name);
			continue;
		}
		Tools.Add(Tool);
		ToolsByName.Add(Name, Tool);
	}
}

bool FAgentMcpServerImpl::HandleRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	switch (Request.Verb)
	{
	case EHttpServerRequestVerbs::VERB_POST:
		return HandlePost(Request, OnComplete);
	case EHttpServerRequestVerbs::VERB_DELETE:
		return HandleDelete(Request, OnComplete);
	default:
		return HandleMethodNotAllowed(Request, OnComplete);
	}
}

bool FAgentMcpServerImpl::IsAuthorized(const FHttpServerRequest& Request) const
{
	if (Config.AuthToken.IsEmpty())
	{
		return true;
	}
	const FString Authorization = UE::AgentMcp::ServerPrivate::GetHeader(Request, TEXT("Authorization"));
	return Authorization.Equals(TEXT("Bearer ") + Config.AuthToken, ESearchCase::CaseSensitive);
}

void FAgentMcpServerImpl::EvictSessions()
{
	using namespace UE::AgentMcp::ServerPrivate;

	const double NowSeconds = FPlatformTime::Seconds();
	for (auto It = Sessions.CreateIterator(); It; ++It)
	{
		const TSharedRef<FAgentMcpSession>& Session = It.Value();
		if (Session->ActiveRequests.Num() == 0 && NowSeconds - Session->LastSeenSeconds > SessionIdleTimeoutSeconds)
		{
			It.RemoveCurrent();
		}
	}

	const int32 MaxSessions = FMath::Max(1, Config.MaxSessions);
	while (Sessions.Num() >= MaxSessions)
	{
		FString OldestSessionId;
		double OldestSeenSeconds = TNumericLimits<double>::Max();
		for (const TPair<FString, TSharedRef<FAgentMcpSession>>& SessionPair : Sessions)
		{
			if (SessionPair.Value->ActiveRequests.Num() == 0 && SessionPair.Value->LastSeenSeconds < OldestSeenSeconds)
			{
				OldestSeenSeconds = SessionPair.Value->LastSeenSeconds;
				OldestSessionId = SessionPair.Key;
			}
		}
		if (OldestSessionId.IsEmpty())
		{
			break;
		}
		Sessions.Remove(OldestSessionId);
	}
}

bool FAgentMcpServerImpl::HandleMethodNotAllowed(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	using namespace UE::AgentMcp::ServerPrivate;

	// UE 5.5 HTTPServer cannot stream responses, so no SSE stream is offered on GET (allowed by the MCP transport spec).
	TUniquePtr<FHttpServerResponse> Response = MakeEmptyResponse(EHttpServerResponseCodes::BadMethod, FString());
	Response->Headers.Add(TEXT("Allow"), { TEXT("POST, DELETE") });
	OnComplete(MoveTemp(Response));
	return true;
}

bool FAgentMcpServerImpl::HandleDelete(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	using namespace UE::AgentMcp::ServerPrivate;

	if (!IsOriginAllowed(Request))
	{
		OnComplete(MakeEmptyResponse(EHttpServerResponseCodes::Forbidden, FString()));
		return true;
	}
	if (!IsAuthorized(Request))
	{
		OnComplete(MakeEmptyResponse(EHttpServerResponseCodes::Denied, FString()));
		return true;
	}

	const FString SessionId = GetHeader(Request, SessionIdHeader);
	const TSharedRef<FAgentMcpSession>* Session = SessionId.IsEmpty() ? nullptr : Sessions.Find(SessionId);
	if (!Session)
	{
		OnComplete(MakeEmptyResponse(EHttpServerResponseCodes::NotFound, FString()));
		return true;
	}

	for (TPair<FString, TSharedRef<bool>>& RequestPair : (*Session)->ActiveRequests)
	{
		*RequestPair.Value = true;
	}
	Sessions.Remove(SessionId);

	UE_LOG(LogAgentMcpProtocol, Log, TEXT("MCP session %s closed by client."), *SessionId);
	OnComplete(MakeEmptyResponse(EHttpServerResponseCodes::Ok, FString()));
	return true;
}

bool FAgentMcpServerImpl::HandlePost(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	using namespace UE::AgentMcp::ServerPrivate;

	if (!IsOriginAllowed(Request))
	{
		UE_LOG(LogAgentMcpProtocol, Warning, TEXT("Rejected MCP request with Origin '%s'."), *GetHeader(Request, TEXT("Origin")));
		OnComplete(MakeJsonResponse(MakeJsonRpcError(nullptr, JsonRpcInvalidRequest, TEXT("Origin not allowed.")), EHttpServerResponseCodes::Forbidden, FString()));
		return true;
	}
	if (!IsAuthorized(Request))
	{
		OnComplete(MakeJsonResponse(MakeJsonRpcError(nullptr, JsonRpcInvalidRequest, TEXT("Missing or invalid bearer token.")), EHttpServerResponseCodes::Denied, FString()));
		return true;
	}
	if (Request.Body.Num() > Config.MaxRequestBytes)
	{
		OnComplete(MakeJsonResponse(MakeJsonRpcError(nullptr, JsonRpcInvalidRequest, TEXT("Request body too large.")), EHttpServerResponseCodes::RequestTooLarge, FString()));
		return true;
	}

	const FString BodyText = UE::AgentMcp::Compat::Utf8ToString(Request.Body);
	TSharedPtr<FJsonValue> ParsedBody;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyText);
	if (!FJsonSerializer::Deserialize(Reader, ParsedBody) || !ParsedBody.IsValid())
	{
		OnComplete(MakeJsonResponse(MakeJsonRpcError(nullptr, JsonRpcParseError, TEXT("Request body is not valid JSON.")), EHttpServerResponseCodes::BadRequest, FString()));
		return true;
	}
	if (ParsedBody->Type == EJson::Array)
	{
		OnComplete(MakeJsonResponse(MakeJsonRpcError(nullptr, JsonRpcInvalidRequest, TEXT("JSON-RPC batch requests are not supported.")), EHttpServerResponseCodes::BadRequest, FString()));
		return true;
	}

	const TSharedPtr<FJsonObject> Message = ParsedBody->Type == EJson::Object ? ParsedBody->AsObject() : nullptr;
	FString JsonRpc;
	if (!Message.IsValid() || !Message->TryGetStringField(TEXT("jsonrpc"), JsonRpc) || JsonRpc != UE::AgentMcp::JsonRpcVersion)
	{
		OnComplete(MakeJsonResponse(MakeJsonRpcError(nullptr, JsonRpcInvalidRequest, TEXT("Expected a JSON-RPC 2.0 message object.")), EHttpServerResponseCodes::BadRequest, FString()));
		return true;
	}

	const TSharedPtr<FJsonValue> Id = Message->TryGetField(TEXT("id"));
	const bool bIsNotification = !Id.IsValid() || Id->Type == EJson::Null;

	FString Method;
	if (!Message->TryGetStringField(TEXT("method"), Method))
	{
		// A response to a server-initiated request. This server never sends requests, so just acknowledge it.
		OnComplete(MakeEmptyResponse(EHttpServerResponseCodes::Accepted, FString()));
		return true;
	}

	TSharedPtr<FJsonObject> Params;
	const TSharedPtr<FJsonObject>* ParamsObject = nullptr;
	if (Message->TryGetObjectField(TEXT("params"), ParamsObject) && ParamsObject)
	{
		Params = *ParamsObject;
	}

	const FString HeaderProtocolVersion = GetHeader(Request, ProtocolVersionHeader);
	if (!HeaderProtocolVersion.IsEmpty() && !UE::AgentMcp::GetSupportedProtocolVersions().Contains(HeaderProtocolVersion))
	{
		OnComplete(MakeJsonResponse(
			MakeJsonRpcError(Id, JsonRpcInvalidRequest, FString::Printf(TEXT("Unsupported MCP-Protocol-Version '%s'."), *HeaderProtocolVersion)),
			EHttpServerResponseCodes::BadRequest, FString()));
		return true;
	}

	if (Method == TEXT("initialize"))
	{
		if (bIsNotification)
		{
			OnComplete(MakeJsonResponse(MakeJsonRpcError(nullptr, JsonRpcInvalidRequest, TEXT("initialize must be a request with an id.")), EHttpServerResponseCodes::BadRequest, FString()));
			return true;
		}
		HandleInitialize(Id, Params, OnComplete);
		return true;
	}

	const FString SessionId = GetHeader(Request, SessionIdHeader);
	if (SessionId.IsEmpty())
	{
		OnComplete(MakeJsonResponse(MakeJsonRpcError(Id, JsonRpcInvalidRequest, TEXT("Missing Mcp-Session-Id header. Call initialize first.")), EHttpServerResponseCodes::BadRequest, FString()));
		return true;
	}

	const TSharedRef<FAgentMcpSession>* FoundSession = Sessions.Find(SessionId);
	if (!FoundSession)
	{
		// 404 tells the client to start a new session (MCP Streamable HTTP session management).
		OnComplete(MakeJsonResponse(MakeJsonRpcError(Id, JsonRpcInvalidRequest, TEXT("Unknown or expired session. Call initialize again.")), EHttpServerResponseCodes::NotFound, FString()));
		return true;
	}

	const TSharedRef<FAgentMcpSession> Session = *FoundSession;
	Session->LastSeenSeconds = FPlatformTime::Seconds();

	if (bIsNotification)
	{
		if (Method == TEXT("notifications/initialized"))
		{
			Session->bInitialized = true;
		}
		else if (Method == TEXT("notifications/cancelled") && Params.IsValid())
		{
			const TSharedPtr<FJsonValue> CancelledId = Params->TryGetField(TEXT("requestId"));
			if (const TSharedRef<bool>* CancelFlag = Session->ActiveRequests.Find(RequestIdToKey(CancelledId)))
			{
				**CancelFlag = true;
			}
		}
		OnComplete(MakeEmptyResponse(EHttpServerResponseCodes::Accepted, Session->Id));
		return true;
	}

	if (Method == TEXT("ping"))
	{
		OnComplete(MakeJsonResponse(MakeJsonRpcResult(Id, MakeShared<FJsonObject>()), EHttpServerResponseCodes::Ok, Session->Id));
		return true;
	}
	if (Method == TEXT("tools/list"))
	{
		HandleToolsList(Id, Params, Session->Id, OnComplete);
		return true;
	}
	if (Method == TEXT("tools/call"))
	{
		HandleToolsCall(Id, Params, Session, OnComplete);
		return true;
	}

	OnComplete(MakeJsonResponse(
		MakeJsonRpcError(Id, JsonRpcMethodNotFound, FString::Printf(TEXT("Method not found: %s"), *Method)),
		EHttpServerResponseCodes::Ok, Session->Id));
	return true;
}

void FAgentMcpServerImpl::HandleInitialize(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params, const FHttpResultCallback& OnComplete)
{
	using namespace UE::AgentMcp::ServerPrivate;

	EvictSessions();

	TSharedRef<FAgentMcpSession> Session = MakeShared<FAgentMcpSession>();
	Session->Id = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	Session->LastSeenSeconds = FPlatformTime::Seconds();

	FString RequestedVersion;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("protocolVersion"), RequestedVersion);

		const TSharedPtr<FJsonObject>* ClientInfo = nullptr;
		if (Params->TryGetObjectField(TEXT("clientInfo"), ClientInfo) && ClientInfo && ClientInfo->IsValid())
		{
			(*ClientInfo)->TryGetStringField(TEXT("name"), Session->ClientName);
			(*ClientInfo)->TryGetStringField(TEXT("version"), Session->ClientVersion);
		}
	}
	Session->ProtocolVersion = UE::AgentMcp::NegotiateProtocolVersion(RequestedVersion);
	Sessions.Add(Session->Id, Session);

	TSharedRef<FJsonObject> ToolsCapability = MakeShared<FJsonObject>();
	ToolsCapability->SetBoolField(TEXT("listChanged"), false);

	TSharedRef<FJsonObject> Capabilities = MakeShared<FJsonObject>();
	Capabilities->SetObjectField(TEXT("tools"), ToolsCapability);

	TSharedRef<FJsonObject> ServerInfo = MakeShared<FJsonObject>();
	ServerInfo->SetStringField(TEXT("name"), Config.ServerName);
	ServerInfo->SetStringField(TEXT("version"), Config.ServerVersion);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("protocolVersion"), Session->ProtocolVersion);
	Result->SetObjectField(TEXT("capabilities"), Capabilities);
	Result->SetObjectField(TEXT("serverInfo"), ServerInfo);
	if (!Config.Instructions.IsEmpty())
	{
		Result->SetStringField(TEXT("instructions"), Config.Instructions);
	}

	UE_LOG(LogAgentMcpProtocol, Log, TEXT("MCP session %s initialized (client '%s' %s, protocol %s)."),
		*Session->Id, *Session->ClientName, *Session->ClientVersion, *Session->ProtocolVersion);

	OnComplete(MakeJsonResponse(MakeJsonRpcResult(Id, Result), EHttpServerResponseCodes::Ok, Session->Id));
}

void FAgentMcpServerImpl::HandleToolsList(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params, const FString& SessionId, const FHttpResultCallback& OnComplete)
{
	using namespace UE::AgentMcp::ServerPrivate;

	int32 StartIndex = 0;
	FString Cursor;
	if (Params.IsValid() && Params->TryGetStringField(TEXT("cursor"), Cursor) && !Cursor.IsEmpty())
	{
		const int32 ParsedCursor = Cursor.IsNumeric() ? FCString::Atoi(*Cursor) : INDEX_NONE;
		if (ParsedCursor < 0 || ParsedCursor > Tools.Num())
		{
			OnComplete(MakeJsonResponse(MakeJsonRpcError(Id, JsonRpcInvalidParams, TEXT("Invalid pagination cursor.")), EHttpServerResponseCodes::Ok, SessionId));
			return;
		}
		StartIndex = ParsedCursor;
	}

	const int32 EndIndex = FMath::Min(Tools.Num(), StartIndex + FMath::Max(1, Config.ToolsListPageSize));

	TArray<TSharedPtr<FJsonValue>> ToolArray;
	ToolArray.Reserve(EndIndex - StartIndex);
	for (int32 Index = StartIndex; Index < EndIndex; ++Index)
	{
		const TSharedRef<IAgentMcpTool>& Tool = Tools[Index];

		TSharedRef<FJsonObject> ToolObject = MakeShared<FJsonObject>();
		ToolObject->SetStringField(TEXT("name"), Tool->GetName());
		ToolObject->SetStringField(TEXT("description"), Tool->GetDescription());
		ToolObject->SetObjectField(TEXT("inputSchema"), Tool->GetInputSchema());
		if (TSharedPtr<FJsonObject> Annotations = Tool->GetAnnotations())
		{
			ToolObject->SetObjectField(TEXT("annotations"), Annotations);
		}
		ToolArray.Add(MakeShared<FJsonValueObject>(ToolObject));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("tools"), ToolArray);
	if (EndIndex < Tools.Num())
	{
		Result->SetStringField(TEXT("nextCursor"), FString::FromInt(EndIndex));
	}

	OnComplete(MakeJsonResponse(MakeJsonRpcResult(Id, Result), EHttpServerResponseCodes::Ok, SessionId));
}

void FAgentMcpServerImpl::HandleToolsCall(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params, const TSharedRef<FAgentMcpSession>& Session, const FHttpResultCallback& OnComplete)
{
	using namespace UE::AgentMcp::ServerPrivate;

	FString ToolName;
	if (!Params.IsValid() || !Params->TryGetStringField(TEXT("name"), ToolName) || ToolName.IsEmpty())
	{
		OnComplete(MakeJsonResponse(MakeJsonRpcError(Id, JsonRpcInvalidParams, TEXT("tools/call requires a non-empty 'name'.")), EHttpServerResponseCodes::Ok, Session->Id));
		return;
	}

	const TSharedRef<IAgentMcpTool>* FoundTool = ToolsByName.Find(ToolName);
	if (!FoundTool)
	{
		OnComplete(MakeJsonResponse(MakeJsonRpcError(Id, JsonRpcInvalidParams, FString::Printf(TEXT("Unknown tool: %s"), *ToolName)), EHttpServerResponseCodes::Ok, Session->Id));
		return;
	}

	TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
	if (const TSharedPtr<FJsonValue> ArgumentsValue = Params->TryGetField(TEXT("arguments")))
	{
		if (ArgumentsValue->Type == EJson::Object && ArgumentsValue->AsObject().IsValid())
		{
			Arguments = ArgumentsValue->AsObject().ToSharedRef();
		}
		else if (ArgumentsValue->Type != EJson::Null)
		{
			OnComplete(MakeJsonResponse(MakeJsonRpcError(Id, JsonRpcInvalidParams, TEXT("tools/call 'arguments' must be an object.")), EHttpServerResponseCodes::Ok, Session->Id));
			return;
		}
	}

	FAgentMcpCallContext Context;
	Context.SessionId = Session->Id;
	Context.RequestKey = RequestIdToKey(Id);
	Session->ActiveRequests.Add(Context.RequestKey, Context.CancelFlag);

	const TSharedRef<IAgentMcpTool> Tool = *FoundTool;
	const double StartSeconds = FPlatformTime::Seconds();
	const TWeakPtr<FAgentMcpServerImpl> WeakSelf = AsShared();
	const TSharedRef<bool> bCompleted = MakeShared<bool>(false);
	const FString SessionId = Session->Id;
	const FString RequestKey = Context.RequestKey;
	const TSharedRef<bool> CancelFlag = Context.CancelFlag;

	Tool->Run(Arguments, Context,
		[WeakSelf, OnComplete, Id, SessionId, RequestKey, CancelFlag, ToolName, StartSeconds, bCompleted](FAgentMcpToolResult&& Result)
		{
			if (*bCompleted)
			{
				UE_LOG(LogAgentMcpProtocol, Warning, TEXT("Tool '%s' completed more than once; ignoring the extra completion."), *ToolName);
				return;
			}
			*bCompleted = true;

			if (const TSharedPtr<FAgentMcpServerImpl> Pinned = WeakSelf.Pin())
			{
				if (const TSharedRef<FAgentMcpSession>* FoundSession = Pinned->Sessions.Find(SessionId))
				{
					(*FoundSession)->ActiveRequests.Remove(RequestKey);
				}
			}

			const double DurationMs = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
			if (*CancelFlag)
			{
				UE_LOG(LogAgentMcpProtocol, Log, TEXT("tools/call '%s' cancelled after %.1f ms."), *ToolName, DurationMs);
				OnComplete(UE::AgentMcp::ServerPrivate::MakeJsonResponse(
					UE::AgentMcp::ServerPrivate::MakeJsonRpcError(Id, UE::AgentMcp::ServerPrivate::JsonRpcRequestCancelled, TEXT("Request cancelled by client.")),
					EHttpServerResponseCodes::Ok, SessionId));
				return;
			}

			UE_LOG(LogAgentMcpProtocol, Log, TEXT("tools/call '%s' %s in %.1f ms."),
				*ToolName, Result.bIsError ? TEXT("returned an error") : TEXT("succeeded"), DurationMs);
			OnComplete(UE::AgentMcp::ServerPrivate::MakeJsonResponse(
				UE::AgentMcp::ServerPrivate::MakeJsonRpcResult(Id, Result.ToJson()),
				EHttpServerResponseCodes::Ok, SessionId));
		});
}

FAgentMcpServer::FAgentMcpServer()
	: Impl(MakeShared<FAgentMcpServerImpl>())
{
}

FAgentMcpServer::~FAgentMcpServer()
{
	Impl->Stop();
}

bool FAgentMcpServer::Start(const FAgentMcpServerConfig& Config, FString& OutError)
{
	const bool bStarted = Impl->Start(Config, OutError);
	if (bStarted)
	{
		UE_LOG(LogAgentMcpProtocol, Log, TEXT("Agent MCP server listening on %s (%d tools)."), *GetEndpointUrl(), Impl->Tools.Num());
	}
	else
	{
		UE_LOG(LogAgentMcpProtocol, Error, TEXT("Agent MCP server failed to start: %s"), *OutError);
	}
	return bStarted;
}

void FAgentMcpServer::Stop()
{
	if (Impl->bRunning)
	{
		UE_LOG(LogAgentMcpProtocol, Log, TEXT("Agent MCP server stopped."));
	}
	Impl->Stop();
}

bool FAgentMcpServer::IsRunning() const
{
	return Impl->bRunning;
}

const FAgentMcpServerConfig& FAgentMcpServer::GetConfig() const
{
	return Impl->Config;
}

FString FAgentMcpServer::GetEndpointUrl() const
{
	return FString::Printf(TEXT("http://127.0.0.1:%u%s"), Impl->Config.Port, *Impl->Config.UrlPath);
}

void FAgentMcpServer::SetTools(const TArray<TSharedRef<IAgentMcpTool>>& Tools)
{
	Impl->SetTools(Tools);
}

int32 FAgentMcpServer::GetToolCount() const
{
	return Impl->Tools.Num();
}
