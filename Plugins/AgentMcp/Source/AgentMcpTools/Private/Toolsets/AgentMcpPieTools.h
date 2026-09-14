#pragma once

#include "CoreMinimal.h"
#include "AgentMcpAsyncResult.h"
#include "AgentMcpToolset.h"
#include "AgentMcpToolTypes.h"

#include "AgentMcpPieTools.generated.h"

USTRUCT(BlueprintType)
struct FAgentMcpPieStartResult
{
	GENERATED_BODY()

	UPROPERTY()
	FAgentMcpPlaySessionState PlaySession;

	/** Log sequence before the session was requested. Pass it to log_get_recent as sinceSequence to read the session log. */
	UPROPERTY()
	int64 StartLogSequence = 0;

	/** Seconds from the request until the play world began play. */
	UPROPERTY()
	double StartupSeconds = 0.0;
};

USTRUCT(BlueprintType)
struct FAgentMcpPieStopResult
{
	GENERATED_BODY()

	/** A session was starting or running when the call was made. */
	UPROPERTY()
	bool bWasActive = false;

	/** Seconds until the session had shut down. */
	UPROPERTY()
	double ShutdownSeconds = 0.0;

	UPROPERTY()
	FAgentMcpPlaySessionState PlaySession;

	/** Latest log sequence after shutdown. */
	UPROPERTY()
	int64 LatestLogSequence = 0;
};

USTRUCT(BlueprintType)
struct FAgentMcpPieStatusResult
{
	GENERATED_BODY()

	UPROPERTY()
	FAgentMcpPlaySessionState PlaySession;

	/** Latest log sequence; pass it to log_get_recent as sinceSequence to read only newer lines. */
	UPROPERTY()
	int64 LatestLogSequence = 0;
};

/** Play In Editor session control. */
UCLASS(meta = (McpToolset = "pie"))
class UAgentMcpPieTools : public UAgentMcpToolset
{
	GENERATED_BODY()

public:
	/**
	 * Starts Play In Editor (or Simulate) in the level viewport and waits until the play world has begun play.
	 * Refuses to start while loaded Blueprints have compile errors, because the editor would stop on a confirmation dialog.
	 * @param bSimulate Simulate In Editor instead of Play In Editor.
	 * @param WarmupSeconds Seconds to wait after BeginPlay so startup logic and logs settle (0-60).
	 * @param TimeoutSeconds Maximum seconds to wait for the session to begin play (5-600).
	 * @return Play session state, and the log sequence from which to read the session log.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|PIE", meta = (AICallable, McpAccess = "Control", BlueprintInternalUseOnly = "true"))
	static UAgentMcpAsyncResult* Start(bool bSimulate = false, float WarmupSeconds = 1.0f, float TimeoutSeconds = 120.0f);

	/**
	 * Stops the play session and waits until it has shut down. Succeeds with bWasActive false when no session is running.
	 * @param TimeoutSeconds Maximum seconds to wait for the shutdown (5-600).
	 * @return Whether a session was running, and the resulting state.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|PIE", meta = (AICallable, McpAccess = "Control", BlueprintInternalUseOnly = "true"))
	static UAgentMcpAsyncResult* Stop(float TimeoutSeconds = 60.0f);

	/**
	 * Reports whether a play session is starting or running, its world and its game time.
	 * @return Play session state.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|PIE", meta = (AICallable, McpAccess = "Read", McpName = "status", BlueprintInternalUseOnly = "true"))
	static FAgentMcpPieStatusResult GetStatus();
};
