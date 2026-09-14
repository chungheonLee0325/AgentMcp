#pragma once

#include "CoreMinimal.h"
#include "AgentMcpAsyncResult.h"
#include "AgentMcpToolset.h"

#include "AgentMcpTestbedFixtureTools.generated.h"

class AActor;
class UBlueprint;

USTRUCT(BlueprintType)
struct FAgentMcpTestbedFixtures
{
	GENERATED_BODY()

	/** Object path of the DataTable fixture. */
	UPROPERTY()
	FString DataTable;

	/** Row names after the reset. */
	UPROPERTY()
	TArray<FString> Rows;

	/** Widget Blueprint fixtures: WBP_AgentMcpBound compiles, WBP_AgentMcpMissingBinding lacks its required BindWidget. */
	UPROPERTY()
	TArray<FString> WidgetBlueprints;

	/** Assets created by this call. */
	UPROPERTY()
	TArray<FString> Created;

	/** Assets deleted by this call: the assets that the smoke test creates. */
	UPROPERTY()
	TArray<FString> Deleted;

	/** Every fixture package was saved. */
	UPROPERTY()
	bool bSaved = false;
};

USTRUCT(BlueprintType)
struct FAgentMcpTestbedHookResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString Message;
};

/** Smoke test fixtures and test hooks of the AgentMcp testbed. Not part of the AgentMcp plugin. */
UCLASS(meta = (McpToolset = "testbed"))
class UAgentMcpTestbedFixtureTools : public UAgentMcpToolset
{
	GENERATED_BODY()

public:
	/**
	 * Creates or resets the smoke test fixtures and clears the undo history: /Game/AgentMcpFixtures/DT_AgentMcpSmoke with the rows
	 * Alpha, Beta and Gamma, and the Widget Blueprints WBP_AgentMcpBound and WBP_AgentMcpMissingBinding, compiled and saved. Deletes
	 * /Game/AgentMcpFixtures/WBP_AgentMcpAuthoring and WBP_AgentMcpAuthoringPart, which the smoke test creates with the umg tools.
	 * @return Fixture paths.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Testbed", meta = (AICallable, McpAccess = "Control", BlueprintInternalUseOnly = "true"))
	static FAgentMcpTestbedFixtures ResetFixtures();

	/**
	 * Test hook: adds a tag to the actor and then fails on purpose, to check that the dispatcher rolls back partial writes.
	 * @param Actor The actor to change.
	 * @return Never returns normally; the call fails with TESTBED_FAILURE.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Testbed", meta = (AICallable, McpAccess = "Write", BlueprintInternalUseOnly = "true"))
	static FAgentMcpTestbedHookResult WriteThenFail(AActor* Actor);

	/**
	 * Test hook: an asynchronous call that finishes after the given time, to check cancellation.
	 * @param Seconds Seconds to wait (0-120).
	 * @return Finishes after the wait.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Testbed", meta = (AICallable, McpAccess = "Control", BlueprintInternalUseOnly = "true"))
	static UAgentMcpAsyncResult* WaitSeconds(float Seconds = 5.0f);

	/**
	 * Test hook: sets whether PIE asks for confirmation while this Blueprint has compile errors. The editor clears this flag when the
	 * user chooses to play anyway.
	 * @param Blueprint The Blueprint.
	 * @param bEnabled Ask for confirmation again.
	 * @return The new state.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Testbed", meta = (AICallable, McpAccess = "Control", BlueprintInternalUseOnly = "true"))
	static FAgentMcpTestbedHookResult SetPieWarning(UBlueprint* Blueprint, bool bEnabled);
};
