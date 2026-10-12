#pragma once

#include "CoreMinimal.h"
#include "AgentMcpAsyncResult.h"
#include "AgentMcpToolset.h"
#include "AgentMcpToolTypes.h"

#include "AgentMcpBlueprintTools.generated.h"

class UBlueprint;

USTRUCT(BlueprintType)
struct FAgentMcpBlueprintComponent
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString ClassName;

	/** Component this one is attached to; empty for roots and non-scene components. */
	UPROPERTY()
	FString Parent;

	/** Socket on the parent component. */
	UPROPERTY()
	FString Socket;

	/** Native for components created in a C++ constructor, otherwise the path of the Blueprint that adds the component. */
	UPROPERTY()
	FString DefinedIn;

	/**
	 * Object holding the component's defaults, which object_set_properties changes: a component of the default object for C++
	 * components, the component template for components a Blueprint adds. A template in a parent Blueprint changes that Blueprint.
	 */
	UPROPERTY()
	FString Template;
};

USTRUCT(BlueprintType)
struct FAgentMcpBlueprintVariable
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	/** Blueprint type as shown in the editor, for example Integer or Array of Name. */
	UPROPERTY()
	FString Type;

	UPROPERTY()
	FString Category;

	UPROPERTY()
	bool bInstanceEditable = false;

	UPROPERTY()
	bool bBlueprintReadOnly = false;

	UPROPERTY()
	bool bExposeOnSpawn = false;

	/** None, Replicated or RepNotify. */
	UPROPERTY()
	FString Replication;

	UPROPERTY()
	FString DefaultValue;
};

USTRUCT(BlueprintType)
struct FAgentMcpBlueprintParameter
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString Type;

	/** Output parameter or return value. */
	UPROPERTY()
	bool bOutput = false;
};

USTRUCT(BlueprintType)
struct FAgentMcpBlueprintFunction
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	/** Function (own function graph), Event (custom event) or Override (implements a function or event of a parent class). */
	UPROPERTY()
	FString Kind;

	UPROPERTY()
	bool bPure = false;

	/** Public, Protected or Private. */
	UPROPERTY()
	FString Access;

	UPROPERTY()
	TArray<FAgentMcpBlueprintParameter> Parameters;
};

USTRUCT(BlueprintType)
struct FAgentMcpBlueprintDetails
{
	GENERATED_BODY()

	UPROPERTY()
	FString Blueprint;

	/** Normal, Const, MacroLibrary, Interface, LevelScript or FunctionLibrary. */
	UPROPERTY()
	FString BlueprintKind;

	/** UpToDate, UpToDateWithWarnings, Dirty, Error, BeingCreated or Unknown. */
	UPROPERTY()
	FString Status;

	UPROPERTY()
	FString GeneratedClass;

	UPROPERTY()
	FString ParentClass;

	/** Parent classes from the direct parent up to the first C++ class. */
	UPROPERTY()
	TArray<FString> ParentChain;

	/** First C++ class in the parent chain and its header. */
	UPROPERTY()
	FAgentMcpNativeClassInfo NativeParent;

	UPROPERTY()
	TArray<FString> Interfaces;

	/** Components from C++ constructors, parent Blueprints and this Blueprint. */
	UPROPERTY()
	TArray<FAgentMcpBlueprintComponent> Components;

	/** Variables declared in this Blueprint. */
	UPROPERTY()
	TArray<FAgentMcpBlueprintVariable> Variables;

	/** Functions and events compiled into this Blueprint class. */
	UPROPERTY()
	TArray<FAgentMcpBlueprintFunction> Functions;

	/** Event dispatchers declared in this Blueprint. */
	UPROPERTY()
	TArray<FString> EventDispatchers;

	UPROPERTY()
	TArray<FString> EventGraphs;

	UPROPERTY()
	TArray<FString> MacroGraphs;

	/** Lists were cut at maxItems. */
	UPROPERTY()
	bool bTruncated = false;
};

USTRUCT(BlueprintType)
struct FAgentMcpCompileMessage
{
	GENERATED_BODY()

	/** Error, Warning or Info. */
	UPROPERTY()
	FString Severity;

	UPROPERTY()
	FString Message;

	/** Graph nodes, widgets or other objects the message refers to. */
	UPROPERTY()
	TArray<FString> Objects;
};

USTRUCT(BlueprintType)
struct FAgentMcpBlueprintCompileResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString Blueprint;

	/** Status after compiling: UpToDate, UpToDateWithWarnings or Error. */
	UPROPERTY()
	FString Status;

	/** Number of distinct errors. */
	UPROPERTY()
	int32 ErrorCount = 0;

	/** Number of distinct warnings. */
	UPROPERTY()
	int32 WarningCount = 0;

	/** Distinct errors and warnings, plus notes when there are errors or warnings. */
	UPROPERTY()
	TArray<FAgentMcpCompileMessage> Messages;

	/** The Blueprint package has unsaved changes after compiling; save with asset_save. */
	UPROPERTY()
	bool bDirty = false;

	UPROPERTY()
	double DurationSeconds = 0.0;
};

USTRUCT(BlueprintType)
struct FAgentMcpGraphPin
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	/** Input or Output. */
	UPROPERTY()
	FString Direction;

	/** Pin type as shown in the editor, for example Exec, Float or Actor Object Reference. */
	UPROPERTY()
	FString Type;

	/** Value of an input that is not connected. */
	UPROPERTY()
	FString DefaultValue;

	/** Connected pins as Node.Pin. */
	UPROPERTY()
	TArray<FString> LinkedTo;
};

USTRUCT(BlueprintType)
struct FAgentMcpGraphNode
{
	GENERATED_BODY()

	/** Node name, unique in its graph. Compile messages end their node paths with it. */
	UPROPERTY()
	FString Name;

	/** Title as in the node menu of the editor, for example Print String or Event BeginPlay. */
	UPROPERTY()
	FString Title;

	UPROPERTY()
	FString NodeClass;

	UPROPERTY()
	int32 X = 0;

	UPROPERTY()
	int32 Y = 0;

	/** Disabled or DevelopmentOnly; empty for enabled nodes. Disabled nodes are not compiled. */
	UPROPERTY()
	FString EnabledState;

	/** Error, warning or note of the node from the last compile. */
	UPROPERTY()
	FString CompilerMessage;

	/** Visible pins. */
	UPROPERTY()
	TArray<FAgentMcpGraphPin> Pins;
};

USTRUCT(BlueprintType)
struct FAgentMcpGraphDetails
{
	GENERATED_BODY()

	UPROPERTY()
	FString Blueprint;

	UPROPERTY()
	FString Graph;

	/** Nodes in the graph before filtering. */
	UPROPERTY()
	int32 TotalNodes = 0;

	UPROPERTY()
	TArray<FAgentMcpGraphNode> Nodes;

	/** More nodes matched than maxNodes. */
	UPROPERTY()
	bool bTruncated = false;
};

USTRUCT(BlueprintType)
struct FAgentMcpNodeType
{
	GENERATED_BODY()

	/** Category|Name without spaces, as blueprint_edit_graph adds the node. */
	UPROPERTY()
	FString TypeId;

	/** Set when several node types share the TypeId: the class that declares this one, to pass as declaringClass. */
	UPROPERTY()
	FString DeclaringClass;

	UPROPERTY()
	FString Description;
};

USTRUCT(BlueprintType)
struct FAgentMcpNodeTypeList
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FAgentMcpNodeType> NodeTypes;

	/** More node types matched than maxItems. */
	UPROPERTY()
	bool bTruncated = false;
};

UENUM(BlueprintType)
enum class EAgentMcpGraphEditOp : uint8
{
	/** Adds a node of typeId at x, y and names it ref for later operations of the call. */
	Add,
	/** Removes node. */
	Remove,
	/** Connects the output pin from to the input pin to. */
	Connect,
	/** Breaks the link between the pins from and to. */
	Break,
	/** Sets the value of the input pin to value. */
	SetDefault,
	/** Adds an input or case pin to node: Sequence, Switch, Make Array, Select or a math operator. */
	AddPin,
	/** Moves node to x, y. */
	Move,
};

USTRUCT(BlueprintType)
struct FAgentMcpGraphEdit
{
	GENERATED_BODY()

	UPROPERTY()
	EAgentMcpGraphEditOp Op = EAgentMcpGraphEditOp::Add;

	/** Add: node type from blueprint_find_node_types, or AddEvent|Custom|<EventName> for a new custom event. */
	UPROPERTY()
	FString TypeId;

	/** Add: class that declares the function, variable or event when several node types share the TypeId. */
	UPROPERTY()
	FString DeclaringClass;

	/** Add: name that later operations of this call use for the new node. */
	UPROPERTY()
	FString Ref;

	/** Remove, AddPin, Move: node name or ref. */
	UPROPERTY()
	FString Node;

	/** Connect, Break: output pin as Node.Pin, where Node is a node name or ref. SetDefault: the input pin. */
	UPROPERTY()
	FString From;

	/** Connect, Break: input pin as Node.Pin. */
	UPROPERTY()
	FString To;

	/** SetDefault: value as the editor writes it, for example 3.5, true, Hello or /Game/Meshes/SM_Rock.SM_Rock. */
	UPROPERTY()
	FString Value;

	/** Add, Move: graph position. */
	UPROPERTY()
	int32 X = 0;

	UPROPERTY()
	int32 Y = 0;
};

USTRUCT(BlueprintType)
struct FAgentMcpGraphEditResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString Blueprint;

	UPROPERTY()
	FString Graph;

	/** Node name of each ref. An event that the graph already had keeps its node. */
	UPROPERTY()
	TMap<FString, FString> Refs;

	/** Nodes that the call added or changed, including conversion nodes that a connection inserted. */
	UPROPERTY()
	TArray<FAgentMcpGraphNode> Nodes;
};

USTRUCT(BlueprintType)
struct FAgentMcpNewParameter
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	/** Type, as for variables. */
	UPROPERTY()
	FString Type;
};

USTRUCT(BlueprintType)
struct FAgentMcpNewVariable
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	/**
	 * Boolean, Byte, Integer, Integer64, Float, Name, String or Text; a struct (Vector, Rotator, Transform, LinearColor or a struct path);
	 * an enum (EMovementMode or an enum path); a class for an object reference (Actor, /Game/Enemies/BP_Enemy.BP_Enemy_C); Class of
	 * <class> for a class reference. Array of <type>, Set of <type> and Map of <key> to <value> make containers. The pin types that
	 * blueprint_get_graph prints, such as Linear Color Structure or Actor Object Reference, are accepted too.
	 */
	UPROPERTY()
	FString Type;

	/** Value as the editor writes it, for example 100, true, (X=1,Y=2,Z=3) or /Game/Meshes/SM_Rock.SM_Rock. Not for containers. */
	UPROPERTY()
	FString DefaultValue;

	UPROPERTY()
	FString Category;

	/** Editable on placed actors (Instance Editable). */
	UPROPERTY()
	bool bInstanceEditable = false;

	/** Shown as an input of Spawn Actor and Construct Object nodes. */
	UPROPERTY()
	bool bExposeOnSpawn = false;
};

USTRUCT(BlueprintType)
struct FAgentMcpNewFunction
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	TArray<FAgentMcpNewParameter> Inputs;

	UPROPERTY()
	TArray<FAgentMcpNewParameter> Outputs;

	/** A pure function has no execution pins and runs for each node that reads its outputs. */
	UPROPERTY()
	bool bPure = false;
};

USTRUCT(BlueprintType)
struct FAgentMcpNewEventDispatcher
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	/** Parameters that the dispatcher passes to its bound events. */
	UPROPERTY()
	TArray<FAgentMcpNewParameter> Inputs;
};

USTRUCT(BlueprintType)
struct FAgentMcpAddedFunction
{
	GENERATED_BODY()

	/** Graph of the function, to pass to blueprint_edit_graph. */
	UPROPERTY()
	FString Graph;

	/** The entry node, and the return node when the function has outputs, with their pins. */
	UPROPERTY()
	TArray<FAgentMcpGraphNode> Nodes;
};

USTRUCT(BlueprintType)
struct FAgentMcpBlueprintMembersResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString Blueprint;

	UPROPERTY()
	TArray<FString> Variables;

	UPROPERTY()
	TArray<FAgentMcpAddedFunction> Functions;

	UPROPERTY()
	TArray<FString> EventDispatchers;
};

USTRUCT(BlueprintType)
struct FAgentMcpGraphText
{
	GENERATED_BODY()

	UPROPERTY()
	FString Blueprint;

	/** The graphs as pseudo-code, one section per graph. */
	UPROPERTY()
	FString Text;

	/** The text was cut at maxChars. */
	UPROPERTY()
	bool bTruncated = false;
};

USTRUCT(BlueprintType)
struct FAgentMcpBlueprintUsage
{
	GENERATED_BODY()

	/** The Blueprint, or the level whose level Blueprint matched. */
	UPROPERTY()
	FString Blueprint;

	/** Graph of the matching node; empty for variables and components. */
	UPROPERTY()
	FString Graph;

	/** Node name, to read its chain with blueprint_get_graph and connectedTo. */
	UPROPERTY()
	FString Node;

	/** Node class, as blueprint_get_graph names it (K2Node_CallFunction, K2Node_VariableSet), or Variable or Component. */
	UPROPERTY()
	FString Kind;

	/** Node title, or the name of the variable or component. */
	UPROPERTY()
	FString Title;

	/** Pins of the node that matched the query. */
	UPROPERTY()
	TArray<FString> Matches;

	/** Comment of the node. */
	UPROPERTY()
	FString Comment;
};

USTRUCT(BlueprintType)
struct FAgentMcpBlueprintUsageResult
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FAgentMcpBlueprintUsage> Usages;

	/** Blueprints with at least one match. */
	UPROPERTY()
	int32 BlueprintCount = 0;

	/** More usages matched than maxResults. */
	UPROPERTY()
	bool bTruncated = false;

	/** Blueprints the search index has no data for, which were not searched. Indexing all Blueprints in the Find in Blueprints tab adds them. */
	UPROPERTY()
	int32 UnindexedBlueprints = 0;

	/** Searched Blueprints whose search data an older engine version wrote; newer kinds of data may be missing from them. */
	UPROPERTY()
	int32 OutOfDateBlueprints = 0;
};

/** Blueprint inspection, compilation and graph editing. */
UCLASS(meta = (McpToolset = "blueprint"))
class UAgentMcpBlueprintTools : public UAgentMcpToolset
{
	GENERATED_BODY()

public:
	/**
	 * Describes a Blueprint: parent chain up to the C++ class and its header, interfaces, components (C++, parent Blueprints and own),
	 * variables, functions and events with parameters, event dispatchers and graphs. Works for Actor, Widget and other Blueprints.
	 * @param Blueprint The Blueprint asset. The generated class path (..._C) and the package name are also accepted.
	 * @param bIncludeComponents List components.
	 * @param bIncludeVariables List variables.
	 * @param bIncludeFunctions List functions and events.
	 * @param MaxItems Maximum number of entries per list (1-1000).
	 * @return Blueprint description.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Blueprint", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpBlueprintDetails Inspect(UBlueprint* Blueprint, bool bIncludeComponents = true, bool bIncludeVariables = true, bool bIncludeFunctions = true, int32 MaxItems = 200);

	/**
	 * Compiles a Blueprint, including Widget Blueprints, and returns the status with the compiler errors and warnings.
	 * Compile errors are part of the result, not a tool error. Does not save.
	 * @param Blueprint The Blueprint asset. The generated class path (..._C) and the package name are also accepted.
	 * @return Status and compiler messages.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Blueprint", meta = (AICallable, McpAccess = "Control", BlueprintInternalUseOnly = "true"))
	static FAgentMcpBlueprintCompileResult Compile(UBlueprint* Blueprint);

	/**
	 * Lists the nodes of a Blueprint graph with their pins, values and links. Links read Node.Pin, so a chain can be followed from node
	 * to node. To read one event chain of a large graph, find its event with bEntryPointsOnly, then pass its name as connectedTo.
	 * @param Blueprint The Blueprint asset.
	 * @param Graph Event graph, function, macro or collapsed graph name; blueprint_inspect lists them.
	 * @param Title Case-insensitive part of the node title, or a wildcard pattern.
	 * @param bEntryPointsOnly Only nodes that start execution: events, function entries and macro inputs.
	 * @param ConnectedTo Only the named node and the nodes linked to it, directly or through other nodes.
	 * @param MaxNodes Maximum number of nodes (1-500).
	 * @return The nodes.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Blueprint", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpGraphDetails GetGraph(UBlueprint* Blueprint, const FString& Graph = TEXT("EventGraph"), const FString& Title = TEXT(""), bool bEntryPointsOnly = false, const FString& ConnectedTo = TEXT(""), int32 MaxNodes = 100);

	/**
	 * Finds node types that can be added to a graph, as in the node menu of the editor: functions, events, variables, flow control,
	 * casts, macros and operators. A type id is Category|Name without spaces, for example Development|PrintString,
	 * Utilities|FlowControl|Branch or AddEvent|EventBeginPlay. A filter ending in | lists a category. Node types whose name is the filter
	 * come first, then names that start with it.
	 * @param Blueprint The Blueprint asset.
	 * @param Graph Graph the nodes would go into.
	 * @param Filter Case-insensitive part of the type id, spaces ignored, or a wildcard pattern.
	 * @param ContextPin Node.Pin of a node in the graph: only node types that can connect to it.
	 * @param MaxItems Maximum number of node types (1-500).
	 * @return Matching node types.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Blueprint", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpNodeTypeList FindNodeTypes(UBlueprint* Blueprint, const FString& Graph = TEXT("EventGraph"), const FString& Filter = TEXT(""), const FString& ContextPin = TEXT(""), int32 MaxItems = 50);

	/**
	 * Edits a Blueprint graph with operations applied in order: add, remove and move nodes, connect and break pins, set input values and
	 * add pins. Nodes are named by their node name or, when added by this call, by their ref. Node types and node names are checked
	 * before anything changes, and a failing operation undoes the whole call. Returns the nodes the call added or changed with their pins,
	 * so a first call can add nodes and a second can wire them by the pin names it returned. Does not compile (blueprint_compile) or save.
	 * @param Blueprint The Blueprint asset, in project content.
	 * @param Graph Graph to edit.
	 * @param Operations Operations in order.
	 * @return Node names of the refs and the added or changed nodes.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Blueprint", meta = (AICallable, McpAccess = "Write", BlueprintInternalUseOnly = "true"))
	static FAgentMcpGraphEditResult EditGraph(UBlueprint* Blueprint, const FString& Graph, const TArray<FAgentMcpGraphEdit>& Operations);

	/**
	 * Adds member variables, functions with inputs and outputs, and event dispatchers to a Blueprint. Names, types and default values are
	 * checked before anything changes, and the call is one undo step. Returns the entry and return nodes of each new function with their
	 * pins, so blueprint_edit_graph can fill the function. Their nodes appear in blueprint_find_node_types at once: search Get<Name>,
	 * Set<Name> or the function name. Does not compile (blueprint_compile) or save.
	 * @param Blueprint The Blueprint asset, in project content.
	 * @param Variables Member variables.
	 * @param Functions Function graphs.
	 * @param EventDispatchers Event dispatchers.
	 * @return Names of the new members and the nodes of the new functions.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Blueprint", meta = (AICallable, McpAccess = "Write", AutoCreateRefTerm = "Variables,Functions,EventDispatchers", BlueprintInternalUseOnly = "true"))
	static FAgentMcpBlueprintMembersResult AddMembers(UBlueprint* Blueprint, const TArray<FAgentMcpNewVariable>& Variables, const TArray<FAgentMcpNewFunction>& Functions, const TArray<FAgentMcpNewEventDispatcher>& EventDispatchers);

	/**
	 * Finds where Blueprints use something, through the index of the editor's Find in Blueprints: calls of a function, reads and writes
	 * of a variable, casts, events, dispatcher bindings, components and comments, in every Blueprint and level Blueprint of the project,
	 * also those that are not loaded. Plain words match node titles, pin names, variable names and comments. The query syntax of Find in
	 * Blueprints narrows a search, for example Nodes("Native Name"=+"SetStaticMesh"), Variables(Name=Health) or
	 * Nodes(Pins(Name=Target && ObjectClass=+"BP_Door")). Blueprints with matches are loaded to name their nodes for blueprint_get_graph.
	 * @param Query Words or a Find in Blueprints query.
	 * @param bIncludeEngineContent Also report Blueprints of the engine and engine plugins.
	 * @param MaxResults Maximum number of usages (1-500).
	 * @param TimeoutSeconds Maximum seconds to wait for the search (5-600).
	 * @return The usages, and how many Blueprints the index does not cover.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Blueprint", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static UAgentMcpAsyncResult* FindUsages(const FString& Query, bool bIncludeEngineContent = false, int32 MaxResults = 100, float TimeoutSeconds = 60.0f);

	/**
	 * Reads Blueprint graphs as short pseudo-code, to understand what a Blueprint does without listing every pin. Each event and
	 * function becomes a block whose statements follow the execution wires; data inputs are written as expressions, Branch as if and
	 * else, other nodes with several execution outputs (Sequence, loops, casts, latent actions) as "on <pin>:" blocks, and a node that
	 * several paths reach as a label with goto. $1, $2 name the outputs of executed nodes. Use blueprint_get_graph for exact pins.
	 * @param Blueprint The Blueprint asset.
	 * @param Graph Graph to read; empty reads the event graphs, the construction script, functions and macros.
	 * @param bNodeNames End each statement with its node name, for blueprint_get_graph and blueprint_edit_graph.
	 * @param MaxChars Maximum length of the text (1000-50000). A large Blueprint reads one graph at a time.
	 * @return The graphs as text.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Blueprint", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpGraphText ReadGraph(UBlueprint* Blueprint, const FString& Graph = TEXT(""), bool bNodeNames = false, int32 MaxChars = 20000);
};
