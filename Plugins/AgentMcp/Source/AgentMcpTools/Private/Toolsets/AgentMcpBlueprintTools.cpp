#include "AgentMcpBlueprintTools.h"

#include "AgentMcpToolsCommon.h"

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphToken.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "HAL/PlatformTime.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/UObjectToken.h"
#include "UObject/Package.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace UE::AgentMcp::BlueprintToolsPrivate
{
	constexpr int32 MaxListItems = 1000;

	const TCHAR* LexBlueprintKind(EBlueprintType Type)
	{
		switch (Type)
		{
		case BPTYPE_Normal: return TEXT("Normal");
		case BPTYPE_Const: return TEXT("Const");
		case BPTYPE_MacroLibrary: return TEXT("MacroLibrary");
		case BPTYPE_Interface: return TEXT("Interface");
		case BPTYPE_LevelScript: return TEXT("LevelScript");
		case BPTYPE_FunctionLibrary: return TEXT("FunctionLibrary");
		default: return TEXT("Unknown");
		}
	}

	const TCHAR* LexStatus(EBlueprintStatus Status)
	{
		switch (Status)
		{
		case BS_Dirty: return TEXT("Dirty");
		case BS_Error: return TEXT("Error");
		case BS_UpToDate: return TEXT("UpToDate");
		case BS_BeingCreated: return TEXT("BeingCreated");
		case BS_UpToDateWithWarnings: return TEXT("UpToDateWithWarnings");
		default: return TEXT("Unknown");
		}
	}

	/** Components that the C++ constructors of the class hierarchy create as default subobjects. */
	void AddNativeComponents(UClass* Class, int32 ItemLimit, TArray<FAgentMcpBlueprintComponent>& OutComponents, bool& bOutTruncated)
	{
		UObject* Defaults = Class ? Class->GetDefaultObject() : nullptr;
		if (!Defaults)
		{
			return;
		}

		TArray<UObject*> Subobjects;
		Defaults->GetDefaultSubobjects(Subobjects);
		for (const UObject* Subobject : Subobjects)
		{
			const UActorComponent* Component = Cast<UActorComponent>(Subobject);
			if (!Component)
			{
				continue;
			}
			if (OutComponents.Num() >= ItemLimit)
			{
				bOutTruncated = true;
				return;
			}

			FAgentMcpBlueprintComponent& Entry = OutComponents.AddDefaulted_GetRef();
			Entry.Name = Component->GetName();
			Entry.ClassName = Component->GetClass()->GetName();
			if (const USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
			{
				if (const USceneComponent* AttachParent = SceneComponent->GetAttachParent())
				{
					Entry.Parent = AttachParent->GetName();
				}
			}
			Entry.DefinedIn = TEXT("Native");
		}
	}

	/** Components added in the Components panel (Simple Construction Script) of a Blueprint. */
	void AddConstructionScriptComponents(const UBlueprint* Source, int32 ItemLimit, TArray<FAgentMcpBlueprintComponent>& OutComponents, bool& bOutTruncated)
	{
		const USimpleConstructionScript* ConstructionScript = Source ? Source->SimpleConstructionScript.Get() : nullptr;
		if (!ConstructionScript)
		{
			return;
		}

		TMap<const USCS_Node*, const USCS_Node*> ParentByNode;
		for (const USCS_Node* Node : ConstructionScript->GetAllNodes())
		{
			if (Node)
			{
				for (const USCS_Node* Child : Node->GetChildNodes())
				{
					ParentByNode.Add(Child, Node);
				}
			}
		}

		const FString DefinedIn = Source->GetPathName();
		for (const USCS_Node* Node : ConstructionScript->GetAllNodes())
		{
			if (!Node)
			{
				continue;
			}
			if (OutComponents.Num() >= ItemLimit)
			{
				bOutTruncated = true;
				return;
			}

			FAgentMcpBlueprintComponent& Entry = OutComponents.AddDefaulted_GetRef();
			Entry.Name = Node->GetVariableName().ToString();
			Entry.ClassName = GetNameSafe(Node->ComponentClass);
			if (const USCS_Node* const* ParentNode = ParentByNode.Find(Node))
			{
				Entry.Parent = (*ParentNode)->GetVariableName().ToString();
			}
			else if (!Node->ParentComponentOrVariableName.IsNone())
			{
				Entry.Parent = Node->ParentComponentOrVariableName.ToString();
			}
			if (!Node->AttachToName.IsNone())
			{
				Entry.Socket = Node->AttachToName.ToString();
			}
			Entry.DefinedIn = DefinedIn;
		}
	}
}

FAgentMcpBlueprintDetails UAgentMcpBlueprintTools::Inspect(UBlueprint* Blueprint, bool bIncludeComponents, bool bIncludeVariables, bool bIncludeFunctions, int32 MaxItems)
{
	using namespace UE::AgentMcp::BlueprintToolsPrivate;

	FAgentMcpBlueprintDetails Details;
	if (!UE::AgentMcp::Tools::RequireObject(Blueprint, TEXT("blueprint")))
	{
		return Details;
	}

	const int32 ItemLimit = FMath::Clamp(MaxItems, 1, MaxListItems);
	UClass* GeneratedClass = Blueprint->GeneratedClass;
	UClass* ParentClass = Blueprint->ParentClass;

	Details.Blueprint = Blueprint->GetPathName();
	Details.BlueprintKind = LexBlueprintKind(Blueprint->BlueprintType.GetValue());
	Details.Status = LexStatus(Blueprint->Status.GetValue());
	Details.GeneratedClass = GetPathNameSafe(GeneratedClass);
	Details.ParentClass = GetPathNameSafe(ParentClass);
	Details.NativeParent = UE::AgentMcp::Tools::MakeNativeClassInfo(UE::AgentMcp::Tools::FindNativeClass(ParentClass));

	for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
	{
		if (Interface.Interface)
		{
			Details.Interfaces.Add(Interface.Interface->GetPathName());
		}
	}

	if (bIncludeComponents)
	{
		AddNativeComponents(GeneratedClass ? GeneratedClass : ParentClass, ItemLimit, Details.Components, Details.bTruncated);
		AddConstructionScriptComponents(Blueprint, ItemLimit, Details.Components, Details.bTruncated);
	}

	for (UClass* Class = ParentClass; Class; Class = Class->GetSuperClass())
	{
		Details.ParentChain.Add(Class->GetPathName());
		if (Class->HasAnyClassFlags(CLASS_Native))
		{
			break;
		}
		if (bIncludeComponents)
		{
			AddConstructionScriptComponents(Cast<UBlueprint>(Class->ClassGeneratedBy), ItemLimit, Details.Components, Details.bTruncated);
		}
	}

	if (bIncludeVariables)
	{
		for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
		{
			if (Details.Variables.Num() >= ItemLimit)
			{
				Details.bTruncated = true;
				break;
			}

			FAgentMcpBlueprintVariable& Entry = Details.Variables.AddDefaulted_GetRef();
			Entry.Name = Variable.VarName.ToString();
			Entry.Type = UEdGraphSchema_K2::TypeToText(Variable.VarType).ToString();
			Entry.Category = Variable.Category.ToString();
			const uint64 Flags = Variable.PropertyFlags;
			Entry.bInstanceEditable = (Flags & CPF_Edit) != 0 && (Flags & CPF_DisableEditOnInstance) == 0;
			Entry.bBlueprintReadOnly = (Flags & CPF_BlueprintReadOnly) != 0;
			Entry.bExposeOnSpawn = Variable.HasMetaData(FBlueprintMetadata::MD_ExposeOnSpawn);
			Entry.Replication = !Variable.RepNotifyFunc.IsNone() ? TEXT("RepNotify") : ((Flags & CPF_Net) != 0 ? TEXT("Replicated") : TEXT("None"));
			Entry.DefaultValue = Variable.DefaultValue;
		}
	}

	if (bIncludeFunctions)
	{
		if (GeneratedClass)
		{
			const UClass* SuperClass = GeneratedClass->GetSuperClass();
			for (TFieldIterator<UFunction> It(GeneratedClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
			{
				const UFunction* Function = *It;
				const FString FunctionName = Function->GetName();
				if (Function->HasAnyFunctionFlags(FUNC_Delegate) || FunctionName.StartsWith(TEXT("ExecuteUbergraph")))
				{
					continue;
				}
				if (Details.Functions.Num() >= ItemLimit)
				{
					Details.bTruncated = true;
					break;
				}

				FAgentMcpBlueprintFunction& Entry = Details.Functions.AddDefaulted_GetRef();
				Entry.Name = FunctionName;
				const bool bOverride = SuperClass && SuperClass->FindFunctionByName(Function->GetFName()) != nullptr;
				const bool bOwnGraph = Blueprint->FunctionGraphs.ContainsByPredicate([Function](const UEdGraph* Graph)
				{
					return Graph && Graph->GetFName() == Function->GetFName();
				});
				Entry.Kind = bOverride ? TEXT("Override") : (bOwnGraph ? TEXT("Function") : TEXT("Event"));
				Entry.bPure = Function->HasAnyFunctionFlags(FUNC_BlueprintPure);
				Entry.Access = Function->HasAnyFunctionFlags(FUNC_Private) ? TEXT("Private") : (Function->HasAnyFunctionFlags(FUNC_Protected) ? TEXT("Protected") : TEXT("Public"));

				for (TFieldIterator<FProperty> ParameterIt(Function); ParameterIt && ParameterIt->HasAnyPropertyFlags(CPF_Parm); ++ParameterIt)
				{
					FAgentMcpBlueprintParameter& Parameter = Entry.Parameters.AddDefaulted_GetRef();
					Parameter.Name = ParameterIt->GetName();
					Parameter.Type = UEdGraphSchema_K2::TypeToText(*ParameterIt).ToString();
					Parameter.bOutput = ParameterIt->HasAnyPropertyFlags(CPF_ReturnParm)
						|| (ParameterIt->HasAnyPropertyFlags(CPF_OutParm) && !ParameterIt->HasAnyPropertyFlags(CPF_ReferenceParm));
				}
			}
		}
		else
		{
			// Never compiled: only the graph names are known.
			for (const UEdGraph* Graph : Blueprint->FunctionGraphs)
			{
				if (!Graph)
				{
					continue;
				}
				if (Details.Functions.Num() >= ItemLimit)
				{
					Details.bTruncated = true;
					break;
				}
				FAgentMcpBlueprintFunction& Entry = Details.Functions.AddDefaulted_GetRef();
				Entry.Name = Graph->GetName();
				Entry.Kind = TEXT("Function");
			}
		}
	}

	const auto AddGraphNames = [ItemLimit, &Details](const TArray<TObjectPtr<UEdGraph>>& Graphs, TArray<FString>& OutNames)
	{
		for (const UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}
			if (OutNames.Num() >= ItemLimit)
			{
				Details.bTruncated = true;
				return;
			}
			OutNames.Add(Graph->GetName());
		}
	};
	AddGraphNames(Blueprint->DelegateSignatureGraphs, Details.EventDispatchers);
	AddGraphNames(Blueprint->UbergraphPages, Details.EventGraphs);
	AddGraphNames(Blueprint->MacroGraphs, Details.MacroGraphs);

	return Details;
}

FAgentMcpBlueprintCompileResult UAgentMcpBlueprintTools::Compile(UBlueprint* Blueprint)
{
	using namespace UE::AgentMcp::BlueprintToolsPrivate;

	FAgentMcpBlueprintCompileResult Result;
	if (!UE::AgentMcp::Tools::RequireObject(Blueprint, TEXT("blueprint")))
	{
		return Result;
	}
	if (GEditor && (GEditor->PlayWorld || GEditor->IsPlaySessionInProgress()))
	{
		UE::AgentMcp::RaiseToolError(TEXT("PIE_ACTIVE"), TEXT("Blueprints are not compiled by tools while a play session is running."), TEXT("Stop the play session first (pie_stop)."));
		return Result;
	}

	FCompilerResultsLog CompileLog;
	CompileLog.SetSourcePath(Blueprint->GetPathName());
	const double StartTime = FPlatformTime::Seconds();
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &CompileLog);
	Result.DurationSeconds = FPlatformTime::Seconds() - StartTime;

	Result.Blueprint = Blueprint->GetPathName();
	Result.Status = LexStatus(Blueprint->Status.GetValue());
	Result.bDirty = Blueprint->GetPackage()->IsDirty();

	// The same message can be logged more than once: the UMG compiler checks BindWidget properties for the skeleton class and for the
	// generated class (smoke test: every binding message appeared twice). Report each message once and count distinct messages.
	for (const TSharedRef<FTokenizedMessage>& Message : CompileLog.Messages)
	{
		const EMessageSeverity::Type Severity = Message->GetSeverity();
		const bool bError = Severity == EMessageSeverity::Error;
		const bool bWarning = Severity == EMessageSeverity::Warning || Severity == EMessageSeverity::PerformanceWarning;
		const FString SeverityName = bError ? TEXT("Error") : (bWarning ? TEXT("Warning") : TEXT("Info"));
		const FString Text = Message->ToText().ToString();

		FAgentMcpCompileMessage* Entry = Result.Messages.FindByPredicate([&SeverityName, &Text](const FAgentMcpCompileMessage& Existing)
		{
			return Existing.Severity == SeverityName && Existing.Message == Text;
		});
		if (!Entry)
		{
			Entry = &Result.Messages.AddDefaulted_GetRef();
			Entry->Severity = SeverityName;
			Entry->Message = Text;
			Result.ErrorCount += bError ? 1 : 0;
			Result.WarningCount += bWarning ? 1 : 0;
		}

		// FCompilerResultsLog turns node, pin and object arguments into EdGraph tokens (CompilerResultsLog.cpp FEdGraphToken_Create).
		for (const TSharedRef<IMessageToken>& Token : Message->GetMessageTokens())
		{
			const UObject* Object = nullptr;
			if (Token->GetType() == EMessageToken::EdGraph)
			{
				const TSharedRef<FEdGraphToken> GraphToken = StaticCastSharedRef<FEdGraphToken>(Token);
				const UEdGraphPin* Pin = GraphToken->GetPin();
				Object = Pin ? Pin->GetOwningNodeUnchecked() : GraphToken->GetGraphObject();
			}
			else if (Token->GetType() == EMessageToken::Object)
			{
				Object = StaticCastSharedRef<FUObjectToken>(Token)->GetObject().Get();
			}
			if (Object)
			{
				Entry->Objects.AddUnique(Object->GetPathName());
			}
		}
	}

	// Notes are only useful next to errors or warnings.
	if (Result.ErrorCount == 0 && Result.WarningCount == 0)
	{
		Result.Messages.Reset();
	}
	return Result;
}
