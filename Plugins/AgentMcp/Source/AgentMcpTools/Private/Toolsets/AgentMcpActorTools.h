#pragma once

#include "CoreMinimal.h"
#include "AgentMcpToolset.h"
#include "AgentMcpToolTypes.h"
#include "Templates/SubclassOf.h"

#include "AgentMcpActorTools.generated.h"

class AActor;

USTRUCT(BlueprintType)
struct FAgentMcpActorSummary
{
	GENERATED_BODY()

	/** Label shown in the World Outliner. */
	UPROPERTY()
	FString Label;

	/** Object name, unique within its level. */
	UPROPERTY()
	FString Name;

	/** Object path; pass it to other tools to address this actor unambiguously. */
	UPROPERTY()
	FString Path;

	UPROPERTY()
	FString ClassName;

	/** World Outliner folder, empty at the root. */
	UPROPERTY()
	FString Folder;
};

USTRUCT(BlueprintType)
struct FAgentMcpActorFindResult
{
	GENERATED_BODY()

	/** Matching actors on this page, sorted by label. */
	UPROPERTY()
	TArray<FAgentMcpActorSummary> Actors;

	/** Number of matching actors on all pages. */
	UPROPERTY()
	int32 TotalMatched = 0;

	/** Cursor of the next page, -1 on the last page. */
	UPROPERTY()
	int32 NextCursor = -1;

	/** Editor or Play. */
	UPROPERTY()
	FString World;

	/** Package of the searched world. */
	UPROPERTY()
	FString WorldPackage;
};

USTRUCT(BlueprintType)
struct FAgentMcpComponentSummary
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString ClassName;

	/** Object path; pass it to the object tools. */
	UPROPERTY()
	FString Path;

	/** Name of the scene component this one is attached to; empty for the root and for non-scene components. */
	UPROPERTY()
	FString AttachParent;

	UPROPERTY()
	bool bIsRoot = false;
};

USTRUCT(BlueprintType)
struct FAgentMcpActorDetails
{
	GENERATED_BODY()

	UPROPERTY()
	FString Label;

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString Path;

	UPROPERTY()
	FString ClassPath;

	/** Blueprint asset that generated the class; empty for native classes. */
	UPROPERTY()
	FString Blueprint;

	UPROPERTY()
	FString Folder;

	/** Editor or Play. */
	UPROPERTY()
	FString World;

	/** Map package that owns the actor. */
	UPROPERTY()
	FString Level;

	/** External actor package (One File Per Actor); empty when the actor is stored in the map. */
	UPROPERTY()
	FString ExternalPackage;

	UPROPERTY()
	TArray<FString> Tags;

	UPROPERTY()
	bool bHiddenInGame = false;

	UPROPERTY()
	bool bHiddenInEditor = false;

	/** World transform of the root component. */
	UPROPERTY()
	FAgentMcpTransformValue Transform;

	/** Path of the actor this one is attached to. */
	UPROPERTY()
	FString AttachParent;

	/** Paths of actors directly attached to this one. */
	UPROPERTY()
	TArray<FString> AttachedActors;

	UPROPERTY()
	int32 ComponentCount = 0;

	UPROPERTY()
	TArray<FAgentMcpComponentSummary> Components;
};

USTRUCT(BlueprintType)
struct FAgentMcpSetTransformResult
{
	GENERATED_BODY()

	/** Actor path. */
	UPROPERTY()
	FString Actor;

	UPROPERTY()
	FAgentMcpTransformValue Before;

	/** Transform read back after the change. */
	UPROPERTY()
	FAgentMcpTransformValue After;
};

/** Actors in the editor level or the running play session. */
UCLASS(meta = (McpToolset = "actor"))
class UAgentMcpActorTools : public UAgentMcpToolset
{
	GENERATED_BODY()

public:
	/**
	 * Finds actors by label or name, class, tag, World Outliner folder or selection. Results are sorted by label.
	 * @param Name Case-insensitive text contained in the label or object name; * and ? act as wildcards. Empty matches every actor.
	 * @param ActorClass Only actors of this class or a subclass.
	 * @param Tag Only actors that have this tag.
	 * @param Folder Only actors in this World Outliner folder or its subfolders.
	 * @param bSelectedOnly Only actors selected in the editor.
	 * @param World Search the editor level or the running play session.
	 * @param Limit Maximum number of actors to return (1-500).
	 * @param Cursor nextCursor of the previous call; 0 for the first page.
	 * @return One page of matching actors with their object paths.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Actor", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpActorFindResult Find(const FString& Name = TEXT(""), TSubclassOf<AActor> ActorClass = nullptr, const FString& Tag = TEXT(""), const FString& Folder = TEXT(""), bool bSelectedOnly = false, EAgentMcpWorld World = EAgentMcpWorld::Editor, int32 Limit = 50, int32 Cursor = 0);

	/**
	 * Describes an actor: class, Blueprint, folder, level, tags, visibility, world transform, attachment and components.
	 * @param Actor The actor to describe.
	 * @param bIncludeComponents List the components of the actor.
	 * @param MaxComponents Maximum number of components to list (0-500).
	 * @return Actor description.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Actor", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpActorDetails Inspect(AActor* Actor, bool bIncludeComponents = true, int32 MaxComponents = 100);

	/**
	 * Moves, rotates or scales an actor in the editor level. Omitted values keep their current value.
	 * @param Actor The actor to change.
	 * @param Location World location [X, Y, Z] in centimeters.
	 * @param Rotation World rotation [Pitch, Yaw, Roll] in degrees.
	 * @param Scale Scale [X, Y, Z].
	 * @return The world transform before and after the change.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Actor", meta = (AICallable, McpAccess = "Write", AutoCreateRefTerm = "Location,Rotation,Scale", BlueprintInternalUseOnly = "true"))
	static FAgentMcpSetTransformResult SetTransform(AActor* Actor, const TArray<double>& Location, const TArray<double>& Rotation, const TArray<double>& Scale);
};
