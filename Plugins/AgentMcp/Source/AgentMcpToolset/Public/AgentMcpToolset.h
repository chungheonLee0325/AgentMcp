#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "AgentMcpToolset.generated.h"

/**
 * Base class for Agent MCP toolsets.
 *
 * Every static UFUNCTION marked meta=(AICallable) on a subclass becomes an MCP tool named
 * "<McpToolset>_<snake_case function name>". Subclasses are discovered automatically.
 *
 *   UCLASS(meta=(McpToolset="actor"))
 *   UFUNCTION(BlueprintCallable, Category="Agent MCP",
 *             meta=(AICallable, McpAccess="Read", BlueprintInternalUseOnly="true"))
 *
 *   McpAccess      Read | Write | Destructive | Control (dispatcher policy)
 *   McpName        optional tool name override
 *   McpAllowInPIE  allow a Write tool during a play session
 *
 * BlueprintCallable is required on UE 5.5: UHT records C++ default parameter values (CPP_Default_ metadata) only for
 * BlueprintCallable or Exec functions, and without them every parameter becomes required. BlueprintInternalUseOnly keeps
 * the tools out of Blueprint menus. Structs used as parameters or return values must be USTRUCT(BlueprintType).
 *
 * The function comment becomes the tool description; @param lines become parameter descriptions.
 */
UCLASS(Abstract, MinimalAPI)
class UAgentMcpToolset : public UObject
{
	GENERATED_BODY()
};

namespace UE::AgentMcp
{
	/**
	 * Fails the tool call that is currently executing with a structured error.
	 * The first error wins. Outside a tool call the error is only logged.
	 */
	AGENTMCPTOOLSET_API void RaiseToolError(const FString& Code, const FString& Message, const FString& Hint = FString());

	/** True when RaiseToolError was called during the current tool call. */
	AGENTMCPTOOLSET_API bool HasToolError();

	struct FAgentMcpRuntimeInfo
	{
		bool bServerRunning = false;
		FString EndpointUrl;
		int32 RegisteredToolCount = 0;
		int32 ExposedToolCount = 0;
		FString ExposureMode;
		FString LastError;
	};

	/** Server and registry status, for tools such as editor_get_state. */
	AGENTMCPTOOLSET_API FAgentMcpRuntimeInfo GetRuntimeInfo();
}
