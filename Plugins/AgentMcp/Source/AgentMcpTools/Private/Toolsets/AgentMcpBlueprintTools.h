#pragma once

#include "CoreMinimal.h"
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

/** Blueprint inspection and compilation. */
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
};
