#pragma once

#include "CoreMinimal.h"
#include "AgentMcpToolset.h"
#include "JsonObjectWrapper.h"

#include "AgentMcpObjectTools.generated.h"

USTRUCT(BlueprintType)
struct FAgentMcpPropertyInfo
{
	GENERATED_BODY()

	/** Property name; use it as the key in object_get_properties and object_set_properties. */
	UPROPERTY()
	FString Name;

	/** C++ type, for example float, FVector, TArray<FName> or TObjectPtr<UStaticMesh>. */
	UPROPERTY()
	FString Type;

	UPROPERTY()
	FString Category;

	/** Class that declares the property. */
	UPROPERTY()
	FString DeclaredIn;

	/** object_set_properties can change this property on this object. */
	UPROPERTY()
	bool bEditable = false;

	/** Why the property cannot be changed; empty when editable. */
	UPROPERTY()
	FString NotEditableReason;

	/** First line of the property tooltip. */
	UPROPERTY()
	FString Description;
};

USTRUCT(BlueprintType)
struct FAgentMcpPropertyListResult
{
	GENERATED_BODY()

	/** Object path. */
	UPROPERTY()
	FString Object;

	UPROPERTY()
	FString ClassName;

	UPROPERTY()
	TArray<FAgentMcpPropertyInfo> Properties;

	/** Number of matching properties on all pages. */
	UPROPERTY()
	int32 TotalMatched = 0;

	/** Cursor of the next page, -1 on the last page. */
	UPROPERTY()
	int32 NextCursor = -1;
};

USTRUCT(BlueprintType)
struct FAgentMcpPropertyValuesResult
{
	GENERATED_BODY()

	/** Object path. */
	UPROPERTY()
	FString Object;

	/** Property name to value. Object references are object paths. */
	UPROPERTY()
	FJsonObjectWrapper Values;

	/** Requested names that are not properties of the object. */
	UPROPERTY()
	TArray<FString> Missing;

	/** Requested properties that cannot be read: not exposed to the editor or Blueprint, or not representable as JSON. */
	UPROPERTY()
	TArray<FString> Inaccessible;
};

USTRUCT(BlueprintType)
struct FAgentMcpSetPropertiesResult
{
	GENERATED_BODY()

	/** Object path. */
	UPROPERTY()
	FString Object;

	/** Values of the requested properties before the call. */
	UPROPERTY()
	FJsonObjectWrapper Before;

	/** Values read back after the call. */
	UPROPERTY()
	FJsonObjectWrapper After;

	/** Properties whose value changed. */
	UPROPERTY()
	TArray<FString> Changed;

	/** Properties that already had the requested value. */
	UPROPERTY()
	TArray<FString> Unchanged;
};

/** Reflection access to object properties. */
UCLASS(meta = (McpToolset = "object"))
class UAgentMcpObjectTools : public UAgentMcpToolset
{
	GENERATED_BODY()

public:
	/**
	 * Lists the properties of an object with their C++ type and category, and whether object_set_properties can change them.
	 * @param Object The object to list.
	 * @param Filter Case-insensitive text contained in the property name or category.
	 * @param bUserVisibleOnly Only properties exposed to the editor or Blueprint.
	 * @param Limit Maximum number of properties to return (1-500).
	 * @param Cursor nextCursor of the previous call; 0 for the first page.
	 * @return One page of properties.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Object", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpPropertyListResult ListProperties(UObject* Object, const FString& Filter = TEXT(""), bool bUserVisibleOnly = true, int32 Limit = 200, int32 Cursor = 0);

	/**
	 * Reads property values as JSON.
	 * @param Object The object to read.
	 * @param PropertyNames Property names as listed by object_list_properties. Empty reads every property exposed to the editor or Blueprint.
	 * @return Values by property name, and the requested names that do not exist or cannot be read.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Object", meta = (AICallable, McpAccess = "Read", AutoCreateRefTerm = "PropertyNames", BlueprintInternalUseOnly = "true"))
	static FAgentMcpPropertyValuesResult GetProperties(UObject* Object, const TArray<FString>& PropertyNames);

	/**
	 * Changes property values of an actor or component in the editor level, or of a project asset such as a data asset or a texture, with
	 * editor change notifications (PreEditChange and PostEditChangeProperty). Blueprints, DataTables (use the datatable tools) and engine
	 * content are refused. Every value is checked before anything changes. A struct value may list only the fields to change; an array
	 * value replaces the whole array.
	 * @param Object The actor, component or asset to change.
	 * @param Values Property name to new value, for example {"Tags": ["Door"], "bHidden": true}.
	 * @return Values before and after the call, and which properties changed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Object", meta = (AICallable, McpAccess = "Write", BlueprintInternalUseOnly = "true"))
	static FAgentMcpSetPropertiesResult SetProperties(UObject* Object, const FJsonObjectWrapper& Values);
};
