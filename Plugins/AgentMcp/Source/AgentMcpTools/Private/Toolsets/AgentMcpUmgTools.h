#pragma once

#include "CoreMinimal.h"
#include "AgentMcpToolset.h"
#include "AgentMcpToolTypes.h"
#include "Blueprint/UserWidget.h"
#include "Components/PanelWidget.h"
#include "JsonObjectWrapper.h"
#include "Templates/SubclassOf.h"

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

USTRUCT(BlueprintType)
struct FAgentMcpWidgetBlueprintCreateResult
{
	GENERATED_BODY()

	/** Object path of the new Widget Blueprint. */
	UPROPERTY()
	FString WidgetBlueprint;

	UPROPERTY()
	FString ParentClass;

	/** Name of the root widget; empty when the tree has no root. */
	UPROPERTY()
	FString RootWidget;

	UPROPERTY()
	FString RootWidgetClass;

	/** Required BindWidget properties of the parent class; the Blueprint fails to compile until widgets with these names exist. */
	UPROPERTY()
	TArray<FString> MissingBindWidgets;
};

USTRUCT(BlueprintType)
struct FAgentMcpWidgetEditResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString WidgetBlueprint;

	/**
	 * Widgets the call added (in tree order) or changed, with the requested values read back; for umg_remove_widgets the removed
	 * widgets and their descendants in tree order, or for a dry run the widgets that would be removed.
	 */
	UPROPERTY()
	TArray<FAgentMcpWidgetNode> Widgets;

	/** The change was applied. False for a dry run. */
	UPROPERTY()
	bool bApplied = false;

	/** Required BindWidget properties without a widget of the expected class after the call; blueprint_compile fails until they are bound. */
	UPROPERTY()
	TArray<FString> MissingBindWidgets;

	/** Consequences to check, such as BindWidget properties that lose their widget or graph nodes that are removed, and dry run notes. */
	UPROPERTY()
	TArray<FString> Warnings;
};

/** Widget Blueprint inspection and editing: widget tree, slots, BindWidget properties, animations and property bindings. */
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

	/**
	 * Creates a Widget Blueprint asset under /Game or in a project plugin, optionally with a root panel. The asset is not saved (use
	 * asset_save), its creation cannot be undone with editor_undo, and the tool is blocked during a play session. Before building a screen or
	 * a component, read the umg-authoring skill with skills_get.
	 * @param AssetPath Package path of the new asset, for example /Game/UI/WBP_Hud. No asset may exist there yet.
	 * @param ParentClass UserWidget subclass to derive from, such as a C++ class with BindWidget properties. Empty uses UserWidget.
	 * @param RootWidgetClass Panel class of the root widget, such as CanvasPanel or Overlay. Empty creates no root; umg_add_widgets can add one.
	 * @return The new Widget Blueprint, its root widget and the required BindWidget properties that still need a widget.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|UMG", meta = (AICallable, McpAccess = "Control", BlueprintInternalUseOnly = "true"))
	static FAgentMcpWidgetBlueprintCreateResult CreateWidgetBlueprint(const FString& AssetPath, TSubclassOf<UUserWidget> ParentClass = nullptr, TSubclassOf<UPanelWidget> RootWidgetClass = nullptr);

	/**
	 * Adds widgets to a Widget Blueprint. Each entry describes one widget and, for a panel, its children, so a whole subtree is added in
	 * one call; every entry is checked before anything changes. Entry fields: "class" (required: a widget class such as TextBlock, or a
	 * Widget Blueprint), "name" (unique; generated when omitted), "isVariable" (default false), "properties" (widget property values),
	 * "slot" (values of the slot in the parent, such as LayoutData of a CanvasPanelSlot or Padding of a VerticalBoxSlot) and "children".
	 * A struct value may list only the fields to set. Compile the Blueprint afterwards with blueprint_compile. object_list_properties
	 * on a class default object such as /Script/UMG.Default__TextBlock or /Script/UMG.Default__CanvasPanelSlot lists property names.
	 * Before building a screen or a component, read the umg-authoring skill with skills_get.
	 * @param WidgetBlueprint The Widget Blueprint asset.
	 * @param Widgets Widget entries, for example [{"class": "TextBlock", "name": "Title", "properties": {"Text": "Hello"}, "slot": {"Padding": {"Left": 8}}}].
	 * @param Parent Panel widget to add the entries to. Empty adds the root widget of a tree that has none.
	 * @param Index Position of the first entry among the parent's children; -1 appends.
	 * @return The added widgets in tree order, with the requested values read back.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|UMG", meta = (AICallable, McpAccess = "Write", BlueprintInternalUseOnly = "true"))
	static FAgentMcpWidgetEditResult AddWidgets(UWidgetBlueprint* WidgetBlueprint, const TArray<FJsonObjectWrapper>& Widgets, const FString& Parent = TEXT(""), int32 Index = -1);

	/**
	 * Changes widgets of a Widget Blueprint: widget property values, values of the slot in the parent, and whether a widget is a variable.
	 * Every value is checked before anything changes. A struct value may list only the fields to change; an array value replaces the whole
	 * array. Compile the Blueprint afterwards with blueprint_compile.
	 * @param WidgetBlueprint The Widget Blueprint asset.
	 * @param Widgets Widget name to its changes, for example {"Title": {"properties": {"Text": "Hi"}, "slot": {"Padding": {"Top": 4}}, "isVariable": true}}.
	 * @return The changed widgets with the requested values read back.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|UMG", meta = (AICallable, McpAccess = "Write", BlueprintInternalUseOnly = "true"))
	static FAgentMcpWidgetEditResult SetWidgetProperties(UWidgetBlueprint* WidgetBlueprint, const FJsonObjectWrapper& Widgets);

	/**
	 * Removes widgets and their descendants from a Widget Blueprint, together with the property bindings of the removed widgets and the
	 * graph nodes that use their variables. Without bConfirm this is a dry run that lists the widgets and the consequences. Compile the
	 * Blueprint afterwards with blueprint_compile.
	 * @param WidgetBlueprint The Widget Blueprint asset.
	 * @param WidgetNames Widgets to remove.
	 * @param bConfirm Remove the widgets. False only checks the request.
	 * @return The removed widgets, or those a dry run would remove, with warnings about bindings and graph references.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|UMG", meta = (AICallable, McpAccess = "Destructive", BlueprintInternalUseOnly = "true"))
	static FAgentMcpWidgetEditResult RemoveWidgets(UWidgetBlueprint* WidgetBlueprint, const TArray<FString>& WidgetNames, bool bConfirm = false);
};
