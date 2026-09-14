#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"

class IAgentMcpTool;

struct FAgentMcpServerConfig
{
	uint32 Port = 18765;
	FString UrlPath = TEXT("/mcp");

	/** When non-empty, requests must carry "Authorization: Bearer <token>". */
	FString AuthToken;

	int32 MaxRequestBytes = 8 * 1024 * 1024;
	int32 ToolsListPageSize = 500;
	int32 MaxSessions = 32;

	FString ServerName = TEXT("agent-mcp");
	FString ServerVersion = TEXT("0.1.0");

	/** Returned from initialize as agent-facing usage guidance. */
	FString Instructions;
};

/**
 * MCP server over UE's HTTPServer module (Streamable HTTP transport, JSON responses only).
 * POST handles JSON-RPC, GET answers 405 (no SSE stream on UE 5.5), DELETE ends a session.
 * Requests are handled on the game thread (HTTPServer ticks there).
 */
class AGENTMCPPROTOCOL_API FAgentMcpServer
{
public:
	FAgentMcpServer();
	~FAgentMcpServer();

	FAgentMcpServer(const FAgentMcpServer&) = delete;
	FAgentMcpServer& operator=(const FAgentMcpServer&) = delete;

	bool Start(const FAgentMcpServerConfig& Config, FString& OutError);
	void Stop();
	bool IsRunning() const;

	const FAgentMcpServerConfig& GetConfig() const;
	FString GetEndpointUrl() const;

	/** Replaces the exposed tool set. Tools with invalid or duplicate names are skipped and logged. */
	void SetTools(const TArray<TSharedRef<IAgentMcpTool>>& Tools);
	int32 GetToolCount() const;

private:
	TSharedRef<class FAgentMcpServerImpl> Impl;
};
