#pragma once

#include "CoreMinimal.h"

AGENTMCPPROTOCOL_API DECLARE_LOG_CATEGORY_EXTERN(LogAgentMcpProtocol, Log, All);

namespace UE::AgentMcp
{
	inline constexpr const TCHAR* JsonRpcVersion = TEXT("2.0");

	/** Protocol version answered when a client requests a version this server does not know. */
	inline constexpr const TCHAR* FallbackProtocolVersion = TEXT("2025-06-18");

	/** Protocol versions accepted during initialize, newest first. */
	AGENTMCPPROTOCOL_API const TArray<FString>& GetSupportedProtocolVersions();

	/** Returns the requested version when supported, otherwise FallbackProtocolVersion. */
	AGENTMCPPROTOCOL_API FString NegotiateProtocolVersion(const FString& RequestedVersion);

	/** MCP tool name rule: 1 to 128 characters of [A-Za-z0-9_.-]. */
	AGENTMCPPROTOCOL_API bool IsValidMcpToolName(const FString& Name);
}
