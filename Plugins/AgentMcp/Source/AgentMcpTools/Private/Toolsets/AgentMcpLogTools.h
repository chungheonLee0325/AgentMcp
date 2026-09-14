#pragma once

#include "CoreMinimal.h"
#include "AgentMcpToolset.h"

#include "AgentMcpLogTools.generated.h"

UENUM(BlueprintType)
enum class EAgentMcpLogVerbosity : uint8
{
	Fatal,
	Error,
	Warning,
	Display,
	Log,
	Verbose,
	VeryVerbose,
};

USTRUCT(BlueprintType)
struct FAgentMcpLogLine
{
	GENERATED_BODY()

	UPROPERTY()
	int64 Sequence = 0;

	/** UTC time, ISO 8601. */
	UPROPERTY()
	FString Time;

	UPROPERTY()
	FString Category;

	UPROPERTY()
	FString Verbosity;

	UPROPERTY()
	FString Message;
};

USTRUCT(BlueprintType)
struct FAgentMcpLogRecentResult
{
	GENERATED_BODY()

	/** Matching lines, oldest first. */
	UPROPERTY()
	TArray<FAgentMcpLogLine> Entries;

	/** Pass back as sinceSequence to read only newer lines. */
	UPROPERTY()
	int64 NextSequence = 0;

	/** Oldest line still buffered; older lines were dropped. */
	UPROPERTY()
	int64 OldestAvailableSequence = 0;

	/** More matching lines exist than were returned. */
	UPROPERTY()
	bool bTruncated = false;
};

/** Reads the editor output log captured by the plugin, with incremental polling. */
UCLASS(meta = (McpToolset = "log"))
class UAgentMcpLogTools : public UAgentMcpToolset
{
	GENERATED_BODY()

public:
	/**
	 * Returns recent editor log lines captured since the plugin loaded.
	 * @param SinceSequence Only lines after this sequence number; pass nextSequence from the previous call. 0 returns the latest lines.
	 * @param MinVerbosity Least severe level to include.
	 * @param Categories Comma-separated log categories to include, for example "LogBlueprint,LogTemp". Empty includes all.
	 * @param Contains Case-insensitive text a line must contain.
	 * @param MaxEntries Maximum number of lines to return (1-2000).
	 * @return Lines oldest first, with nextSequence for incremental polling.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Log", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpLogRecentResult GetRecent(int64 SinceSequence = 0, EAgentMcpLogVerbosity MinVerbosity = EAgentMcpLogVerbosity::Log, const FString& Categories = TEXT(""), const FString& Contains = TEXT(""), int32 MaxEntries = 200);
};
