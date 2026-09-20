#pragma once

#include "CoreMinimal.h"
#include "AgentMcpToolset.h"

#include "AgentMcpLevelTools.generated.h"

USTRUCT(BlueprintType)
struct FAgentMcpLevelResult
{
	GENERATED_BODY()

	/** Package of the level that is now open, for example /Game/Maps/Greybox. */
	UPROPERTY()
	FString Level;

	/** World Partition level: its actors are saved in their own packages. */
	UPROPERTY()
	bool bPartitioned = false;

	/** Level the new level was copied from, when a template was used. */
	UPROPERTY()
	FString Template;

	/** Level packages with unsaved changes after the call; level_save writes them. */
	UPROPERTY()
	TArray<FString> UnsavedLevels;
};

USTRUCT(BlueprintType)
struct FAgentMcpLevelSaveResult
{
	GENERATED_BODY()

	/** Level packages that were dirty before and are saved now. */
	UPROPERTY()
	TArray<FString> Levels;

	/** Level packages that are still dirty, for example because saving them failed. */
	UPROPERTY()
	TArray<FString> UnsavedLevels;

	UPROPERTY()
	TArray<FString> Warnings;
};

/**
 * The level open in the editor: creating one, opening one and saving it.
 *
 * These tools replace the world in the editor, which discards unsaved level changes, so level_new and level_open refuse to run while
 * a saved level has unsaved changes. An untitled scratch world under /Temp is not protected, because no tool can save it.
 * asset_save still refuses levels; levels are saved with level_save.
 */
UCLASS(meta = (McpToolset = "level"))
class UAgentMcpLevelTools : public UAgentMcpToolset
{
	GENERATED_BODY()

public:
	/**
	 * Creates a level, saves it to the given path and opens it. Refuses a path that already has an asset.
	 * @param AssetPath Package path of the new level under /Game or a project plugin, for example /Game/Maps/Greybox.
	 * @param bPartitioned Create a World Partition level, whose actors are saved in their own packages. A template decides this itself.
	 * @param TemplatePath Level to copy instead of starting empty, for example /Game/Maps/Kit or an engine template.
	 * @return The level that is now open.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Level", meta = (AICallable, McpAccess = "Control", BlueprintInternalUseOnly = "true"))
	static FAgentMcpLevelResult New(const FString& AssetPath, bool bPartitioned = true, const FString& TemplatePath = TEXT(""));

	/**
	 * Opens an existing level in the editor.
	 * @param AssetPath Package path of the level, for example /Game/Maps/Greybox.
	 * @return The level that is now open.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Level", meta = (AICallable, McpAccess = "Control", BlueprintInternalUseOnly = "true"))
	static FAgentMcpLevelResult Open(const FString& AssetPath);

	/**
	 * Saves the level open in the editor, the only tool that writes a level to disk.
	 * @param bAllDirty Save every level with unsaved changes instead of the current one alone.
	 * @return The levels that were saved.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Level", meta = (AICallable, McpAccess = "Control", BlueprintInternalUseOnly = "true"))
	static FAgentMcpLevelSaveResult Save(bool bAllDirty = false);
};
