#pragma once

#include "CoreMinimal.h"
#include "AgentMcpToolset.h"
#include "JsonObjectWrapper.h"

#include "AgentMcpDataTableTools.generated.h"

class UDataTable;

USTRUCT(BlueprintType)
struct FAgentMcpDataTableColumn
{
	GENERATED_BODY()

	/** Column name as shown in the DataTable editor and used as the key in row values. */
	UPROPERTY()
	FString Name;

	/** Property name when it differs from the column name (user-defined structs add generated suffixes). */
	UPROPERTY()
	FString PropertyName;

	/** C++ type, for example int32, FText or TSoftObjectPtr<UTexture2D>. */
	UPROPERTY()
	FString Type;

	UPROPERTY()
	FString Category;

	/** First line of the property tooltip. */
	UPROPERTY()
	FString Description;

	/** JSON Schema of the column value. */
	UPROPERTY()
	FJsonObjectWrapper JsonSchema;
};

USTRUCT(BlueprintType)
struct FAgentMcpDataTableSchema
{
	GENERATED_BODY()

	UPROPERTY()
	FString DataTable;

	/** Row struct path, for example /Script/MyGame.ItemRow. */
	UPROPERTY()
	FString RowStruct;

	/** Module package of a C++ row struct. */
	UPROPERTY()
	FString RowStructModule;

	/** Header of a C++ row struct relative to its module (ModuleRelativePath metadata). */
	UPROPERTY()
	FString RowStructHeader;

	/** The row struct is a user-defined struct asset rather than C++. */
	UPROPERTY()
	bool bUserDefinedStruct = false;

	UPROPERTY()
	int32 RowCount = 0;

	UPROPERTY()
	TArray<FAgentMcpDataTableColumn> Columns;
};

USTRUCT(BlueprintType)
struct FAgentMcpDataTableRowList
{
	GENERATED_BODY()

	UPROPERTY()
	FString DataTable;

	/** Rows in the table. */
	UPROPERTY()
	int32 RowCount = 0;

	/** Matching row names on this page, in table order. */
	UPROPERTY()
	TArray<FString> Rows;

	UPROPERTY()
	int32 TotalMatched = 0;

	/** Cursor of the next page, -1 on the last page. */
	UPROPERTY()
	int32 NextCursor = -1;
};

USTRUCT(BlueprintType)
struct FAgentMcpDataTableRows
{
	GENERATED_BODY()

	UPROPERTY()
	FString DataTable;

	/** Row name to an object of column values. */
	UPROPERTY()
	FJsonObjectWrapper Rows;

	/** Requested rows that do not exist. */
	UPROPERTY()
	TArray<FString> MissingRows;

	/** Requested columns that do not exist. */
	UPROPERTY()
	TArray<FString> MissingColumns;

	/** Rows in the table. */
	UPROPERTY()
	int32 RowCount = 0;

	/** Cursor of the next page when rowNames was empty; -1 on the last page or when rows were named. */
	UPROPERTY()
	int32 NextCursor = -1;
};

USTRUCT(BlueprintType)
struct FAgentMcpDataTableEditResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString DataTable;

	/** Rows that were changed, added, renamed (new names) or removed. For a dry run, the rows that would be removed. */
	UPROPERTY()
	TArray<FString> Rows;

	/** Values of the changed or added rows, read back after the change. */
	UPROPERTY()
	FJsonObjectWrapper After;

	/** The change was applied. False for a dry run. */
	UPROPERTY()
	bool bApplied = false;

	/** Rows in the table after the call. */
	UPROPERTY()
	int32 RowCount = 0;

	/** Source control warnings for saving the DataTable, and dry run notes. */
	UPROPERTY()
	TArray<FString> Warnings;
};

/** DataTable inspection and row editing. */
UCLASS(meta = (McpToolset = "datatable"))
class UAgentMcpDataTableTools : public UAgentMcpToolset
{
	GENERATED_BODY()

public:
	/**
	 * Describes the columns of a DataTable: the row struct and its C++ header, column names, C++ types and JSON schemas.
	 * @param DataTable The DataTable asset.
	 * @return Row struct and columns.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|DataTable", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpDataTableSchema GetSchema(UDataTable* DataTable);

	/**
	 * Lists the row names of a DataTable in table order.
	 * @param DataTable The DataTable asset.
	 * @param NameContains Case-insensitive text contained in the row name; * and ? act as wildcards.
	 * @param Limit Maximum number of row names to return (1-2000).
	 * @param Cursor nextCursor of the previous call; 0 for the first page.
	 * @return One page of row names.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|DataTable", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpDataTableRowList ListRows(UDataTable* DataTable, const FString& NameContains = TEXT(""), int32 Limit = 200, int32 Cursor = 0);

	/**
	 * Reads DataTable rows as JSON, keyed by row name and column name.
	 * @param DataTable The DataTable asset.
	 * @param RowNames Rows to read. Empty reads a page of rows in table order.
	 * @param ColumnNames Columns to include. Empty includes every column.
	 * @param Limit Rows per page when rowNames is empty (1-500).
	 * @param Cursor nextCursor of the previous call when rowNames is empty; 0 for the first page.
	 * @return Row values, and the requested rows and columns that do not exist.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|DataTable", meta = (AICallable, McpAccess = "Read", AutoCreateRefTerm = "RowNames,ColumnNames", BlueprintInternalUseOnly = "true"))
	static FAgentMcpDataTableRows GetRows(UDataTable* DataTable, const TArray<FString>& RowNames, const TArray<FString>& ColumnNames, int32 Limit = 50, int32 Cursor = 0);

	/**
	 * Changes column values of existing rows in a project DataTable. Only the given columns change, and a struct value may list only
	 * the fields to change. Every row and value is checked before anything changes. Does not save; use asset_save.
	 * @param DataTable The DataTable asset.
	 * @param Rows Row name to an object of column name to new value, for example {"Sword": {"Damage": 12}}.
	 * @return Changed rows read back.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|DataTable", meta = (AICallable, McpAccess = "Write", BlueprintInternalUseOnly = "true"))
	static FAgentMcpDataTableEditResult SetRows(UDataTable* DataTable, const FJsonObjectWrapper& Rows);

	/**
	 * Adds rows to a project DataTable, with default values or the given column values. Does not save; use asset_save.
	 * @param DataTable The DataTable asset.
	 * @param RowNames Names of the new rows; they must not exist yet.
	 * @param Values Optional row name to an object of column values for the new rows.
	 * @return Added rows read back.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|DataTable", meta = (AICallable, McpAccess = "Write", AutoCreateRefTerm = "Values", BlueprintInternalUseOnly = "true"))
	static FAgentMcpDataTableEditResult AddRows(UDataTable* DataTable, const TArray<FString>& RowNames, const FJsonObjectWrapper& Values);

	/**
	 * Renames rows of a project DataTable. Row handles that store the old names are not updated. Does not save; use asset_save.
	 * @param DataTable The DataTable asset.
	 * @param Renames Old row name to new row name. New names must not exist yet.
	 * @return New row names.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|DataTable", meta = (AICallable, McpAccess = "Write", BlueprintInternalUseOnly = "true"))
	static FAgentMcpDataTableEditResult RenameRows(UDataTable* DataTable, const TMap<FString, FString>& Renames);

	/**
	 * Removes rows from a project DataTable. Without bConfirm this is a dry run that only checks the request and lists the rows.
	 * Does not save; use asset_save.
	 * @param DataTable The DataTable asset.
	 * @param RowNames Rows to remove.
	 * @param bConfirm Remove the rows. False only checks the request.
	 * @return Rows removed, or the rows a dry run would remove.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|DataTable", meta = (AICallable, McpAccess = "Destructive", BlueprintInternalUseOnly = "true"))
	static FAgentMcpDataTableEditResult RemoveRows(UDataTable* DataTable, const TArray<FString>& RowNames, bool bConfirm = false);
};
