#pragma once

#include "CoreMinimal.h"
#include "AgentMcpToolset.h"

#include "AgentMcpClassTools.generated.h"

USTRUCT(BlueprintType)
struct FAgentMcpDerivedClass
{
	GENERATED_BODY()

	UPROPERTY()
	FString ClassPath;

	UPROPERTY()
	FString Name;

	/** C++ class. False for Blueprint classes. */
	UPROPERTY()
	bool bNative = false;

	/** Blueprint asset of a Blueprint class. */
	UPROPERTY()
	FString Blueprint;

	/** Module package of a C++ class, for example /Script/UMG. */
	UPROPERTY()
	FString Module;

	/** Header of a C++ class relative to its module (ModuleRelativePath metadata). */
	UPROPERTY()
	FString Header;

	/** The class is loaded. Blueprint classes that are not loaded come from the asset registry. */
	UPROPERTY()
	bool bLoaded = false;
};

USTRUCT(BlueprintType)
struct FAgentMcpDerivedClassResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString BaseClass;

	/** Derived classes on this page, sorted by class path. */
	UPROPERTY()
	TArray<FAgentMcpDerivedClass> Classes;

	UPROPERTY()
	int32 TotalMatched = 0;

	/** Cursor of the next page, -1 on the last page. */
	UPROPERTY()
	int32 NextCursor = -1;

	/** The asset registry is still scanning, so Blueprint classes may be missing. */
	UPROPERTY()
	bool bAssetRegistryLoading = false;
};

/** Class hierarchy queries over C++ and Blueprint classes. */
UCLASS(meta = (McpToolset = "class"))
class UAgentMcpClassTools : public UAgentMcpToolset
{
	GENERATED_BODY()

public:
	/**
	 * Finds classes that derive from a class: C++ classes with their headers, and Blueprint classes with their assets,
	 * including Blueprints that are not loaded.
	 * @param BaseClass The base class, for example UserWidget or /Script/MyGame.MyHUDWidget.
	 * @param NameContains Case-insensitive text contained in the class name; * and ? act as wildcards.
	 * @param bIncludeNative Include C++ classes.
	 * @param bIncludeBlueprint Include Blueprint classes.
	 * @param Limit Maximum number of classes to return (1-500).
	 * @param Cursor nextCursor of the previous call; 0 for the first page.
	 * @return One page of derived classes.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Class", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpDerivedClassResult FindDerived(UClass* BaseClass, const FString& NameContains = TEXT(""), bool bIncludeNative = true, bool bIncludeBlueprint = true, int32 Limit = 100, int32 Cursor = 0);
};
