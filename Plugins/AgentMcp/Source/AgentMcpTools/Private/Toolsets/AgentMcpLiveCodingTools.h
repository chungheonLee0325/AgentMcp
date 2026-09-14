#pragma once

#include "CoreMinimal.h"
#include "AgentMcpToolset.h"

#include "AgentMcpLiveCodingTools.generated.h"

USTRUCT(BlueprintType)
struct FAgentMcpLiveCodingResult
{
	GENERATED_BODY()

	/** Success, NoChanges, Failure, Cancelled, CompileStillActive, NotStarted or InProgress. */
	UPROPERTY()
	FString Result;

	UPROPERTY()
	double DurationSeconds = 0.0;

	/** Log sequence before the compile. log_get_recent from it (category LogLiveCoding) shows the compiler output. */
	UPROPERTY()
	int64 StartLogSequence = 0;
};

/** C++ compilation with Live Coding in the running editor. */
UCLASS(meta = (McpToolset = "livecoding"))
class UAgentMcpLiveCodingTools : public UAgentMcpToolset
{
	GENERATED_BODY()

public:
	/**
	 * Compiles changed C++ code with Live Coding and patches the running editor, waiting until the compile has finished.
	 * Changes to reflected declarations (UCLASS, USTRUCT, UPROPERTY, UFUNCTION) and new modules need a full build with the editor closed.
	 * @return Compile result. A failed compile is reported in the result; its details are in the log.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|LiveCoding", meta = (AICallable, McpAccess = "Control", BlueprintInternalUseOnly = "true"))
	static FAgentMcpLiveCodingResult Compile();
};
