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

	/** Width in pixels of the play viewport, which viewport_capture captures. */
	UPROPERTY()
	int32 ViewportWidth = 0;

	/** Height in pixels of the play viewport. */
	UPROPERTY()
	int32 ViewportHeight = 0;
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
	 * Starts Play In Editor (or Simulate) and waits until the play world has begun play. The session plays in the level viewport, whose size
	 * follows the editor window, or, with WindowWidth and WindowHeight, in a new window whose client area has that size, so that captures to
	 * compare have the same size on every run. Refuses to start while loaded Blueprints have compile errors, because the editor would stop
	 * on a confirmation dialog.
	 * @param bSimulate Simulate In Editor instead of Play In Editor. It runs in the level viewport.
	 * @param WarmupSeconds Seconds to wait after BeginPlay so startup logic and logs settle (0-60).
	 * @param TimeoutSeconds Maximum seconds to wait for the session to begin play (5-600).
	 * @param WindowWidth Width in pixels (320-7680) of a new play window, also used for the windows of additional clients. 0 plays in the level viewport.
	 * @param WindowHeight Height in pixels (240-4320) of the new play window. 0 plays in the level viewport.
	 * @return Play session state, the size of the play viewport, and the log sequence from which to read the session log.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|PIE", meta = (AICallable, McpAccess = "Control", BlueprintInternalUseOnly = "true"))
	static UAgentMcpAsyncResult* Start(bool bSimulate = false, float WarmupSeconds = 1.0f, float TimeoutSeconds = 120.0f, int32 WindowWidth = 0, int32 WindowHeight = 0);

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
