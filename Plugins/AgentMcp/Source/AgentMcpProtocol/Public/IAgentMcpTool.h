#pragma once

#include "CoreMinimal.h"
#include "AgentMcpToolResult.h"
#include "Dom/JsonObject.h"
#include "Templates/Function.h"

/** Per-call information handed to a tool. */
struct FAgentMcpCallContext
{
	FString SessionId;
	FString RequestKey;

	/** Set to true when the client sends notifications/cancelled for this request. */
	TSharedRef<bool> CancelFlag = MakeShared<bool>(false);

	bool IsCancelled() const { return *CancelFlag; }
};

using FAgentMcpToolCompletion = TFunction<void(FAgentMcpToolResult&&)>;

/**
 * MCP tool contract served by FAgentMcpServer. A tool answers once when it has finished; progress notifications are not
 * supported because UE 5.5's HTTP server cannot stream a response.
 */
class IAgentMcpTool : public TSharedFromThis<IAgentMcpTool>
{
public:
	virtual ~IAgentMcpTool() = default;

	virtual FString GetName() const = 0;
	virtual FString GetDescription() const = 0;

	/** JSON Schema object with "type":"object". */
	virtual TSharedRef<FJsonObject> GetInputSchema() const = 0;

	/** Optional MCP tool annotations (readOnlyHint, destructiveHint, ...). */
	virtual TSharedPtr<FJsonObject> GetAnnotations() const { return nullptr; }

	/**
	 * Executes the tool. Called on the game thread.
	 * OnComplete must be invoked exactly once, either immediately or later for asynchronous tools.
	 */
	virtual void Run(const TSharedRef<FJsonObject>& Arguments, const FAgentMcpCallContext& Context, FAgentMcpToolCompletion&& OnComplete) = 0;
};
