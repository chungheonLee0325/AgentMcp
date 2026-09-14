#pragma once

#include "CoreMinimal.h"
#include "AgentMcpToolset.h"
#include "AgentMcpToolTypes.h"

#include "AgentMcpEditorTools.generated.h"

USTRUCT(BlueprintType)
struct FAgentMcpUndoState
{
	GENERATED_BODY()

	UPROPERTY()
	bool bCanUndo = false;

	/** Transaction editor_undo would revert. */
	UPROPERTY()
	FString UndoTitle;

	UPROPERTY()
	bool bCanRedo = false;

	/** Number of transactions that can be undone. */
	UPROPERTY()
	int32 UndoableCount = 0;
};

USTRUCT(BlueprintType)
struct FAgentMcpServerState
{
	GENERATED_BODY()

	UPROPERTY()
	bool bRunning = false;

	UPROPERTY()
	FString Endpoint;

	UPROPERTY()
	int32 ExposedTools = 0;

	UPROPERTY()
	FString ExposureMode;

	UPROPERTY()
	FString LastError;
};

USTRUCT(BlueprintType)
struct FAgentMcpProviderState
{
	GENERATED_BODY()

	/** Plugin name. */
	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString Description;
};

USTRUCT(BlueprintType)
struct FAgentMcpEditorState
{
	GENERATED_BODY()

	UPROPERTY()
	FString Project;

	UPROPERTY()
	FString EngineVersion;

	/** Map package open in the level editor. */
	UPROPERTY()
	FString EditorWorld;

	UPROPERTY()
	FAgentMcpPlaySessionState PlaySession;

	/** Unsaved packages (maps and assets). */
	UPROPERTY()
	int32 DirtyPackageCount = 0;

	/** First unsaved packages, sorted. */
	UPROPERTY()
	TArray<FString> DirtyPackages;

	UPROPERTY()
	int32 SelectedActorCount = 0;

	/** Labels of the first selected actors. */
	UPROPERTY()
	TArray<FString> SelectedActors;

	UPROPERTY()
	FAgentMcpUndoState Undo;

	/** Pending shader compile jobs; rendering-related results may still change while non-zero. */
	UPROPERTY()
	int32 ShaderJobsRemaining = 0;

	/** The asset registry is still scanning; asset queries may be incomplete. */
	UPROPERTY()
	bool bAssetRegistryLoading = false;

	UPROPERTY()
	FAgentMcpServerState McpServer;

	/** Other editor automation plugins enabled in this session. Write to an asset through one provider at a time. */
	UPROPERTY()
	TArray<FAgentMcpProviderState> OtherProviders;

	/** Latest log sequence. Pass it to log_get_recent as sinceSequence to read only lines emitted after this call. */
	UPROPERTY()
	int64 LatestLogSequence = 0;
};

USTRUCT(BlueprintType)
struct FAgentMcpUndoResult
{
	GENERATED_BODY()

	/** Transaction that was undone or redone. */
	UPROPERTY()
	FString Transaction;

	/** Undo history after the operation. */
	UPROPERTY()
	FAgentMcpUndoState Undo;
};

/** Editor session state and undo history. */
UCLASS(meta = (McpToolset = "editor"))
class UAgentMcpEditorTools : public UAgentMcpToolset
{
	GENERATED_BODY()

public:
	/**
	 * Summarizes the editor session: open level, play session, unsaved packages, selection, undo history,
	 * pending shader jobs, asset registry scan, MCP server and other automation providers. Call this first.
	 * @param MaxListedItems Maximum number of dirty packages and selected actors to list (0-100).
	 * @return Editor session summary.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Editor", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpEditorState GetState(int32 MaxListedItems = 20);

	/**
	 * Undoes the most recent editor transaction, for example the last Write tool call. Unavailable during a play session.
	 * @return The undone transaction and the resulting undo history.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Editor", meta = (AICallable, McpAccess = "Control", BlueprintInternalUseOnly = "true"))
	static FAgentMcpUndoResult Undo();

	/**
	 * Redoes the most recently undone editor transaction. Unavailable during a play session.
	 * @return The redone transaction and the resulting undo history.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Editor", meta = (AICallable, McpAccess = "Control", BlueprintInternalUseOnly = "true"))
	static FAgentMcpUndoResult Redo();
};
