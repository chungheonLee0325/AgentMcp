#pragma once

#include "CoreMinimal.h"
#include "AgentMcpToolset.h"
#include "AgentMcpToolTypes.h"
#include "JsonObjectWrapper.h"

#include "AgentMcpUmgTools.generated.h"

class UWidgetBlueprint;

USTRUCT(BlueprintType)
struct FAgentMcpWidgetSlot
{
	GENERATED_BODY()

	/** Slot class, for example CanvasPanelSlot or VerticalBoxSlot. Empty for the root widget. */
	UPROPERTY()
	FString ClassName;

	/** Slot properties that differ from the slot class defaults, such as layout, padding, alignment and size. */
	UPROPERTY()
	FJsonObjectWrapper Properties;
};

USTRUCT(BlueprintType)
struct FAgentMcpWidgetNode
{
	GENERATED_BODY()

	/** Pre-order position; parents come before their children. */
	UPROPERTY()
	int32 Index = 0;

	/** Levels below the start widget. */
	UPROPERTY()
	int32 Depth = 0;

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString ClassName;

	/** Class path of a nested user widget (another Widget Blueprint or a C++ UserWidget). */
	UPROPERTY()
	FString WidgetClass;

	/** Name of the parent widget; empty for the start widget. */
	UPROPERTY()
	FString Parent;

	/** Named slot of the parent user widget that holds this widget. */
	UPROPERTY()
	FString NamedSlot;

	/** Accessible as a variable from the Blueprint graph and from C++ BindWidget properties. */
	UPROPERTY()
	bool bIsVariable = false;

	UPROPERTY()
	FAgentMcpWidgetSlot Slot;

	/** Widget properties that differ from the class defaults; filled only with includeProperties. */
	UPROPERTY()
	FJsonObjectWrapper Properties;
};

USTRUCT(BlueprintType)
struct FAgentMcpBindWidget
{
	GENERATED_BODY()

	/** C++ property marked BindWidget or BindWidgetOptional. */
	UPROPERTY()
	FString Property;

	/** Class that declares the property. */
	UPROPERTY()
	FString DeclaredIn;

	/** Widget class the property expects. */
	UPROPERTY()
	FString ExpectedClass;

	UPROPERTY()
	bool bOptional = false;

	/** A widget with the property name exists in this Widget Blueprint or a parent Widget Blueprint. */
	UPROPERTY()
	bool bBound = false;

	/** The widget with that name has the expected class. */
	UPROPERTY()
	bool bTypeMatches = false;

	/** Class of the widget with that name. */
	UPROPERTY()
	FString WidgetClassName;
};

USTRUCT(BlueprintType)
struct FAgentMcpPropertyBinding
{
	GENERATED_BODY()

	UPROPERTY()
	FString Widget;

	UPROPERTY()
	FString Property;

	/** Bound function, or the bound source property. */
	UPROPERTY()
	FString Source;
};

USTRUCT(BlueprintType)
struct FAgentMcpWidgetBlueprintDetails
{
	GENERATED_BODY()

	UPROPERTY()
	FString WidgetBlueprint;

	UPROPERTY()
	FString ParentClass;

	/** First C++ class in the parent chain and its header. */
	UPROPERTY()
	FAgentMcpNativeClassInfo NativeParent;

	/** Widgets in the whole tree, including named slot content. */
	UPROPERTY()
	int32 WidgetCount = 0;

	/** Widgets from the start widget in pre-order, limited by maxDepth and maxWidgets. */
	UPROPERTY()
	TArray<FAgentMcpWidgetNode> Widgets;

	/** Widgets were left out because of maxDepth or maxWidgets. */
	UPROPERTY()
	bool bTruncated = false;

	/** BindWidget properties of the parent classes and whether the tree satisfies them. */
	UPROPERTY()
	TArray<FAgentMcpBindWidget> BindWidgets;

	/** Required BindWidget properties without a widget of the expected class; the Blueprint fails to compile until they are bound. */
	UPROPERTY()
	TArray<FString> MissingBindWidgets;

	UPROPERTY()
	TArray<FString> Animations;

	/** Designer property bindings (Bind dropdown). */
	UPROPERTY()
	TArray<FAgentMcpPropertyBinding> PropertyBindings;
};

/** Widget Blueprint inspection: widget tree, slots, BindWidget properties, animations and property bindings. */
UCLASS(meta = (McpToolset = "umg"))
class UAgentMcpUmgTools : public UAgentMcpToolset
{
	GENERATED_BODY()

public:
	/**
	 * Describes the widget tree of a Widget Blueprint: hierarchy with slots and named slots, the C++ parent and its BindWidget
	 * properties with their binding state, animations and designer property bindings.
	 * @param WidgetBlueprint The Widget Blueprint asset. The package name is also accepted.
	 * @param RootWidget Name of the widget to start from. Empty starts at the root.
	 * @param MaxDepth Levels below the start widget to include (0-64).
	 * @param bIncludeSlots Include slot values that differ from the defaults.
	 * @param bIncludeProperties Include widget property values that differ from the defaults (larger output).
	 * @param MaxWidgets Maximum number of widgets to list (1-2000).
	 * @return Widget tree and bindings.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|UMG", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpWidgetBlueprintDetails Inspect(UWidgetBlueprint* WidgetBlueprint, const FString& RootWidget = TEXT(""), int32 MaxDepth = 32, bool bIncludeSlots = true, bool bIncludeProperties = false, int32 MaxWidgets = 300);
};
