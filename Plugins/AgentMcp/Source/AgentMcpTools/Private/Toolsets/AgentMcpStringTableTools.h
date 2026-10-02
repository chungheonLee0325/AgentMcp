#pragma once

#include "CoreMinimal.h"
#include "AgentMcpToolset.h"

#include "AgentMcpStringTableTools.generated.h"

class UStringTable;

USTRUCT(BlueprintType)
struct FAgentMcpStringTableEntries
{
	GENERATED_BODY()

	/** Object path of the string table, the table id that LOCTABLE and FText properties use. */
	UPROPERTY()
	FString StringTable;

	/** Namespace of the table's texts for localization. */
	UPROPERTY()
	FString Namespace;

	/** Key to source string, in key order, for this page. */
	UPROPERTY()
	TMap<FString, FString> Entries;

	/** Entries in the table. */
	UPROPERTY()
	int32 EntryCount = 0;

	/** Entries whose key matches keyContains. */
	UPROPERTY()
	int32 TotalMatched = 0;

	/** Cursor of the next page, -1 on the last page. */
	UPROPERTY()
	int32 NextCursor = -1;
};

USTRUCT(BlueprintType)
struct FAgentMcpStringTableEditResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString StringTable;

	UPROPERTY()
	FString Namespace;

	/** Keys that were added or got a new source string. */
	UPROPERTY()
	TArray<FString> Changed;

	/** Keys that already had the given source string. */
	UPROPERTY()
	TArray<FString> Unchanged;

	/** The namespace was changed. */
	UPROPERTY()
	bool bNamespaceChanged = false;

	/** Entries in the table after the call. */
	UPROPERTY()
	int32 EntryCount = 0;

	/** Source control warnings for saving the string table. */
	UPROPERTY()
	TArray<FString> Warnings;
};

/** String tables: the texts that FText properties and LOCTABLE("<table>", "<key>") reference by table and key. */
UCLASS(meta = (McpToolset = "stringtable"))
class UAgentMcpStringTableTools : public UAgentMcpToolset
{
	GENERATED_BODY()

public:
	/**
	 * Lists the entries of a string table: its namespace, and each key with its source string, in key order.
	 * @param StringTable The string table asset.
	 * @param KeyContains Case-insensitive text contained in the key; * and ? act as wildcards.
	 * @param Limit Maximum number of entries to return (1-2000).
	 * @param Cursor nextCursor of the previous call; 0 for the first page.
	 * @return One page of entries.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|StringTable", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpStringTableEntries GetEntries(UStringTable* StringTable, const FString& KeyContains = TEXT(""), int32 Limit = 500, int32 Cursor = 0);

	/**
	 * Adds entries to a project string table or gives existing keys new source strings, and can set the table's namespace. Texts
	 * that reference an entry show the new string: an FText property set to LOCTABLE("<table object path>", "<key>") with
	 * object_set_properties, or LOCTABLE in C++. Every key and string is checked before anything changes. Does not save; use
	 * asset_save. Create the table with asset_create and assetClass StringTable.
	 * @param StringTable The string table asset.
	 * @param Entries Key to source string, for example {"Result.Success": "Victory"}. Neither may be empty.
	 * @param Namespace Namespace of the table's texts for localization. Empty keeps the current one.
	 * @return The keys that changed and the keys that already had their string.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|StringTable", meta = (AICallable, McpAccess = "Write", BlueprintInternalUseOnly = "true"))
	static FAgentMcpStringTableEditResult SetEntries(UStringTable* StringTable, const TMap<FString, FString>& Entries, const FString& Namespace = TEXT(""));
};
