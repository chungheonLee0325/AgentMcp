#pragma once

#include "CoreMinimal.h"
#include "AgentMcpToolset.h"
#include "AgentMcpBlueprintTools.h"
#include "JsonObjectWrapper.h"
#include "Templates/SubclassOf.h"

#include "AgentMcpAnimationTools.generated.h"

class UAnimInstance;
class USkeleton;

USTRUCT(BlueprintType)
struct FAgentMcpBlendSample
{
	GENERATED_BODY()

	/** Animation sequence of the sample, for example /Game/Anim/AS_Walk. */
	UPROPERTY()
	FString Animation;

	/** Where the sample sits on the axis. */
	UPROPERTY()
	double Value = 0.0;
};

USTRUCT(BlueprintType)
struct FAgentMcpMontageSegment
{
	GENERATED_BODY()

	/** Animation sequence the segment plays. */
	UPROPERTY()
	FString Animation;

	/** Section that starts with this segment. Section names are unique within the montage. */
	UPROPERTY()
	FString Section;

	/** Section that plays when this one ends; the section's own name loops it. Empty plays on to the section that follows, and the
	 * montage ends after the last one. */
	UPROPERTY()
	FString NextSection;

	UPROPERTY()
	double PlayRate = 1.0;

	UPROPERTY()
	int32 LoopCount = 1;
};

USTRUCT(BlueprintType)
struct FAgentMcpAnimVariable
{
	GENERATED_BODY()

	/** Variable name, for example Speed. */
	UPROPERTY()
	FString Name;

	/** Float or Bool. */
	UPROPERTY()
	FString Type;
};

USTRUCT(BlueprintType)
struct FAgentMcpMontageSection
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	/** Section that plays when this one ends; empty where the montage ends. */
	UPROPERTY()
	FString NextSection;

	/** Where the section starts on the montage timeline, in seconds. */
	UPROPERTY()
	double StartSeconds = 0.0;
};

USTRUCT(BlueprintType)
struct FAgentMcpAnimAssetResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString Asset;

	/** The asset was created by this call; otherwise an existing one was rebuilt. */
	UPROPERTY()
	bool bCreated = false;

	/** Montages: the sections as the montage holds them after the build, in timeline order. */
	UPROPERTY()
	TArray<FAgentMcpMontageSection> Sections;

	/** Montages: the play length in seconds. */
	UPROPERTY()
	double PlayLength = 0.0;

	/** Blend spaces: the number of samples. */
	UPROPERTY()
	int32 SampleCount = 0;

	/** The package has unsaved changes; save with asset_save. */
	UPROPERTY()
	bool bDirty = false;
};

USTRUCT(BlueprintType)
struct FAgentMcpAnimBlueprintResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString AnimBlueprint;

	/** The Animation Blueprint was created by this call; otherwise the anim graph of an existing one was rebuilt. */
	UPROPERTY()
	bool bCreated = false;

	UPROPERTY()
	FString ParentClass;

	UPROPERTY()
	FString Skeleton;

	/** Nodes of the anim graph after the build, the output node included. */
	UPROPERTY()
	TArray<FString> Nodes;

	/** Variables this call added to the Blueprint. */
	UPROPERTY()
	TArray<FString> AddedVariables;

	/** The compile after the build. Compile errors are part of the result, not a tool error. */
	UPROPERTY()
	FAgentMcpBlueprintCompileResult Compile;
};

/** Animation assets: blend spaces, montages and the anim graph of Animation Blueprints. */
UCLASS(meta = (McpToolset = "anim"))
class UAgentMcpAnimationTools : public UAgentMcpToolset
{
	GENERATED_BODY()

public:
	/**
	 * Creates a 1D blend space for a skeleton, or rebuilds an existing one: one axis and one sample per animation, placed at its value on
	 * the axis. Every problem with the samples is reported at once and nothing changes. Does not save.
	 * @param AssetPath Package path under /Game or a project plugin, for example /Game/Anim/BS_Locomotion.
	 * @param Skeleton Skeleton the blend space and its animations use.
	 * @param AxisName Name of the axis, for example Speed. An anim graph binds a variable to it.
	 * @param AxisMin Smallest value of the axis.
	 * @param AxisMax Largest value of the axis.
	 * @param Samples Animation sequences of the skeleton and their values, each within the axis.
	 * @param GridDivisions Grid divisions of the axis (1-64).
	 * @param bReplace Rebuild an existing blend space at the path. Without it an existing asset is refused.
	 * @return The blend space and its sample count.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Animation", meta = (AICallable, McpAccess = "Control", BlueprintInternalUseOnly = "true"))
	static FAgentMcpAnimAssetResult BuildBlendSpace(const FString& AssetPath, USkeleton* Skeleton, const FString& AxisName, double AxisMin, double AxisMax,
		const TArray<FAgentMcpBlendSample>& Samples, int32 GridDivisions = 4, bool bReplace = false);

	/**
	 * Creates an animation montage for a skeleton, or rebuilds an existing one: one slot track whose segments play the animations in the
	 * order given, each segment starting a named section. A section can name the section that follows it, so a charge can loop until the
	 * game jumps on. Every problem with the segments is reported at once and nothing changes. Does not save.
	 * @param AssetPath Package path under /Game or a project plugin, for example /Game/Anim/AM_Attack.
	 * @param Skeleton Skeleton the montage and its animations use.
	 * @param Segments Animation sequences in play order, each with its section.
	 * @param SlotName Slot the montage plays in; an anim graph has a Slot node of the same name.
	 * @param BlendInSeconds Blend time into the montage.
	 * @param BlendOutSeconds Blend time out of the montage.
	 * @param bReplace Rebuild an existing montage at the path. Without it an existing asset is refused.
	 * @return The montage, its sections and play length.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Animation", meta = (AICallable, McpAccess = "Control", BlueprintInternalUseOnly = "true"))
	static FAgentMcpAnimAssetResult BuildMontage(const FString& AssetPath, USkeleton* Skeleton, const TArray<FAgentMcpMontageSegment>& Segments,
		const FString& SlotName = TEXT("DefaultSlot"), double BlendInSeconds = 0.25, double BlendOutSeconds = 0.25, bool bReplace = false);

	/**
	 * Creates an Animation Blueprint for a skeleton, or rebuilds the anim graph of an existing one, from a tree of pose nodes that ends in
	 * the output pose, then compiles it. A node is an object with "node" set to one of:
	 *   Slot             "slot" (default DefaultSlot), "source" (a node): plays montages of the slot over its source pose.
	 *   BlendSpacePlayer "blendSpace" (asset path), "x" and optionally "y": a number, or the name of a variable to read each frame.
	 *   SequencePlayer   "sequence" (asset path), "loop" (default true).
	 * A variable is a property of the parent class or a variable of the Blueprint; Variables adds missing Blueprint variables first.
	 * Every problem with the tree is reported at once and nothing changes. Does not save.
	 * @param AssetPath Package path under /Game or a project plugin, for example /Game/Anim/ABP_Hero.
	 * @param Skeleton Skeleton the Animation Blueprint targets.
	 * @param Graph The root node of the tree.
	 * @param Variables Blueprint variables (Float or Bool) to add when the parent class and the Blueprint do not have them.
	 * @param ParentClass AnimInstance subclass to derive from; AnimInstance when empty. An existing Blueprint keeps its parent.
	 * @param bReplace Rebuild the anim graph of an existing Animation Blueprint at the path. Without it an existing asset is refused.
	 * @return The Animation Blueprint, its graph nodes and the compile result.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Animation", meta = (AICallable, McpAccess = "Control", AutoCreateRefTerm = "Variables", BlueprintInternalUseOnly = "true"))
	static FAgentMcpAnimBlueprintResult BuildAnimBlueprint(const FString& AssetPath, USkeleton* Skeleton, const FJsonObjectWrapper& Graph,
		const TArray<FAgentMcpAnimVariable>& Variables, TSubclassOf<UAnimInstance> ParentClass = nullptr, bool bReplace = false);
};
