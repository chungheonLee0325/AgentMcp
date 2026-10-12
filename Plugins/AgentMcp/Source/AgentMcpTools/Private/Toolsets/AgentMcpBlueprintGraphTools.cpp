#include "AgentMcpBlueprintTools.h"

#include "AgentMcpToolsCommon.h"

#include "BlueprintActionDatabase.h"
#include "BlueprintActionFilter.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node.h"
#include "EdGraphNode_Comment.h"
#include "K2Node_AddPinInterface.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Knot.h"
#include "K2Node_Switch.h"
#include "K2Node_Tunnel.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/Kismet2NameValidators.h"

// Node types are found the way UE 5.8 BlueprintEditorLibrary (UBlueprintGraphEditor::CreateNodeFromName) finds them: every node
// spawner of the Blueprint action database that the graph's filter lets through, named Category|MenuName without spaces.
// Names, titles and types are source strings, so they do not change with the editor language (the testbed editor runs in Korean).
namespace UE::AgentMcp::BlueprintGraphToolsPrivate
{
	constexpr int32 MaxGraphNodes = 500;
	constexpr int32 MaxNodeTypes = 500;

	/** Type id of a node added by name as a new custom event: AddEvent|Custom|<EventName>, as in UE 5.8. */
	const FString CustomEventPrefix = TEXT("AddEvent|Custom|");

	UEdGraph* FindGraph(UBlueprint* Blueprint, const FString& GraphName)
	{
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		TArray<FString> Names;
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}
			if (Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
			{
				return Graph;
			}
			Names.Add(Graph->GetName());
		}
		RaiseToolError(TEXT("NOT_FOUND"), FString::Printf(TEXT("%s has no graph '%s'. Graphs: %s."), *Blueprint->GetName(), *GraphName, *FString::Join(Names, TEXT(", "))));
		return nullptr;
	}

	UEdGraphNode* FindNode(const UEdGraph* Graph, const FString& NodeName)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->GetName().Equals(NodeName, ESearchCase::IgnoreCase))
			{
				return Node;
			}
		}
		return nullptr;
	}

	/** First line of the title as the graph shows it. */
	FString GetNodeTitle(const UEdGraphNode& Node)
	{
		FString Title = Node.GetNodeTitle(ENodeTitleType::FullTitle).BuildSourceString();
		int32 LineEnd = INDEX_NONE;
		if (Title.FindChar(TEXT('\n'), LineEnd))
		{
			Title.LeftInline(LineEnd);
		}
		return Title;
	}

	/** Has an execution output and no execution input: events, function entries and macro inputs. */
	bool IsEntryPoint(const UEdGraphNode& Node)
	{
		bool bExecOutput = false;
		for (const UEdGraphPin* Pin : Node.Pins)
		{
			if (Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				if (Pin->Direction == EGPD_Input)
				{
					return false;
				}
				bExecOutput = true;
			}
		}
		return bExecOutput;
	}

	FString MakePinPath(const UEdGraphPin& Pin)
	{
		return FString::Printf(TEXT("%s.%s"), *GetNameSafe(Pin.GetOwningNodeUnchecked()), *Pin.PinName.ToString());
	}

	/**
	 * UEdGraphSchema_K2::TypeToText in source strings. It formats struct, class and enum names from their display names as plain
	 * strings (TerminalTypeToText), so those stay localized in the source string and are swapped back here.
	 */
	FString GetPinTypeText(const FEdGraphPinType& PinType)
	{
		FString Text = UEdGraphSchema_K2::TypeToText(PinType).BuildSourceString();
		for (const UObject* Object : { PinType.PinSubCategoryObject.Get(), PinType.PinValueType.TerminalSubCategoryObject.Get() })
		{
			if (const UField* Field = Cast<UField>(Object))
			{
				const FText DisplayName = Field->GetDisplayNameText();
				Text.ReplaceInline(*FName::NameToDisplayString(DisplayName.ToString(), false), *FName::NameToDisplayString(DisplayName.BuildSourceString(), false));
			}
		}
		return Text;
	}

	FAgentMcpGraphPin MakePin(const UEdGraphPin& Pin)
	{
		FAgentMcpGraphPin Entry;
		Entry.Name = Pin.PinName.ToString();
		Entry.Direction = Pin.Direction == EGPD_Input ? TEXT("Input") : TEXT("Output");
		Entry.Type = GetPinTypeText(Pin.PinType);
		if (Pin.Direction == EGPD_Input && Pin.LinkedTo.IsEmpty() && !Pin.bDefaultValueIsIgnored && Pin.PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
		{
			Entry.DefaultValue = Pin.GetDefaultAsString();
		}
		for (const UEdGraphPin* Linked : Pin.LinkedTo)
		{
			if (Linked)
			{
				Entry.LinkedTo.Add(MakePinPath(*Linked));
			}
		}
		return Entry;
	}

	FAgentMcpGraphNode MakeNode(const UEdGraphNode& Node)
	{
		FAgentMcpGraphNode Entry;
		Entry.Name = Node.GetName();
		Entry.Title = GetNodeTitle(Node);
		Entry.NodeClass = Node.GetClass()->GetName();
		Entry.X = Node.NodePosX;
		Entry.Y = Node.NodePosY;
		switch (Node.GetDesiredEnabledState())
		{
		case ENodeEnabledState::Disabled: Entry.EnabledState = TEXT("Disabled"); break;
		case ENodeEnabledState::DevelopmentOnly: Entry.EnabledState = TEXT("DevelopmentOnly"); break;
		default: break;
		}
		if (Node.bHasCompilerMessage)
		{
			Entry.CompilerMessage = Node.ErrorMsg;
		}
		for (const UEdGraphPin* Pin : Node.Pins)
		{
			if (Pin && !Pin->bHidden)
			{
				Entry.Pins.Add(MakePin(*Pin));
			}
		}
		return Entry;
	}

	FString MakeTypeId(const FBlueprintActionUiSpec& UiSpec)
	{
		return FString::Printf(TEXT("%s|%s"), *UiSpec.Category.BuildSourceString().Replace(TEXT(" "), TEXT("")), *UiSpec.MenuName.BuildSourceString().Replace(TEXT(" "), TEXT("")));
	}

	/**
	 * Calls Visit for every node type that can be added to the graph and, when ContextPin is set, connect to it. Matches is checked
	 * after the filter, which is the cheaper order: the editor's node menu also filters first.
	 */
	void ForEachNodeType(UBlueprint* Blueprint, UEdGraph* Graph, UEdGraphPin* ContextPin,
		TFunctionRef<bool(const FString& TypeId)> Matches,
		TFunctionRef<void(FBlueprintActionInfo& Action, const FString& TypeId, const FBlueprintActionUiSpec& UiSpec)> Visit)
	{
		// The database lists the macros of loaded Blueprints only, and the engine macros (For Loop, Do Once, Is Valid) load with the
		// first Blueprint editor. Loading them registers their actions (BlueprintActionDatabase.cpp OnAssetLoaded).
		LoadObject<UBlueprint>(nullptr, TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"), nullptr, LOAD_NoWarn | LOAD_Quiet);

		// ponytail: walks the whole action database per call, like the editor's node menu; cache per graph if calls get slow.
		FBlueprintActionFilter Filter;
		Filter.Context.Blueprints.Add(Blueprint);
		Filter.Context.Graphs.Add(Graph);
		if (ContextPin)
		{
			Filter.Context.Pins.Add(ContextPin);
		}

		for (const TPair<FObjectKey, FBlueprintActionDatabase::FActionList>& Entry : FBlueprintActionDatabase::Get().GetAllActions())
		{
			const UObject* Owner = Entry.Key.ResolveObjectPtr();
			if (!Owner)
			{
				continue;
			}
			for (const UBlueprintNodeSpawner* Spawner : Entry.Value)
			{
				if (!Spawner)
				{
					continue;
				}
				FBlueprintActionInfo Action(Owner, Spawner);
				if (Filter.IsFiltered(Action))
				{
					continue;
				}
				const FBlueprintActionUiSpec UiSpec = Spawner->GetUiSpec(Filter.Context, Action.GetBindings());
				const FString TypeId = MakeTypeId(UiSpec);
				if (Matches(TypeId))
				{
					Visit(Action, TypeId, UiSpec);
				}
			}
		}
	}

	/**
	 * Pin of a Node.Pin path, where Node is a node name or a key of Refs. Returns null and the problem, without a final period, when the
	 * node or the pin does not exist.
	 */
	UEdGraphPin* FindPin(const UEdGraph* Graph, const FString& PinPath, EEdGraphPinDirection PreferredDirection, const TMap<FString, UEdGraphNode*>& Refs, FString& OutProblem)
	{
		FString NodeName;
		FString PinName;
		if (!PinPath.Split(TEXT("."), &NodeName, &PinName) || NodeName.IsEmpty() || PinName.IsEmpty())
		{
			OutProblem = FString::Printf(TEXT("'%s' is not a pin path; write Node.Pin"), *PinPath);
			return nullptr;
		}

		UEdGraphNode* const* RefNode = Refs.Find(NodeName);
		UEdGraphNode* Node = RefNode ? *RefNode : FindNode(Graph, NodeName);
		if (!Node || !Graph->Nodes.Contains(Node))
		{
			OutProblem = FString::Printf(TEXT("%s has no node '%s'"), *Graph->GetName(), *NodeName);
			return nullptr;
		}

		UEdGraphPin* Pin = Node->FindPin(PinName, PreferredDirection);
		if (!Pin)
		{
			Pin = Node->FindPin(PinName);
		}
		if (!Pin)
		{
			TArray<FString> PinNames;
			for (const UEdGraphPin* Candidate : Node->Pins)
			{
				if (Candidate && !Candidate->bHidden)
				{
					PinNames.Add(FString::Printf(TEXT("%s (%s)"), *Candidate->PinName.ToString(), Candidate->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output")));
				}
			}
			OutProblem = FString::Printf(TEXT("%s has no pin '%s'; its pins are %s"), *Node->GetName(), *PinName, *FString::Join(PinNames, TEXT(", ")));
			return nullptr;
		}
		return Pin;
	}

	/**
	 * How closely a type id matches a filter, best first: 0 the node name (after the last |) is the filter, or the id ends with a filter
	 * that holds a |; 1 the name starts with the filter; 2 the name contains it; 3 only the category does.
	 */
	int32 GetMatchRank(const FString& TypeId, const FString& Pattern)
	{
		if (Pattern.Contains(TEXT("|")))
		{
			return TypeId.EndsWith(Pattern) ? 0 : 1;
		}
		FString Name = TypeId;
		TypeId.Split(TEXT("|"), nullptr, &Name, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		return Name.Equals(Pattern, ESearchCase::IgnoreCase) ? 0 : Name.StartsWith(Pattern) ? 1 : Name.Contains(Pattern) ? 2 : 3;
	}

	/** Node type chosen for an add operation. */
	struct FResolvedType
	{
		const UBlueprintNodeSpawner* Spawner = nullptr;
		const UClass* DeclaringClass = nullptr;
	};

	const UClass* GetAuthoritativeClass(const UClass* Class)
	{
		return Class ? const_cast<UClass*>(Class)->GetAuthoritativeClass() : nullptr;
	}

	/** Why a name was refused, without the final period. Ok means the name is taken by another member of the same call. */
	FString GetNameProblem(const FString& Name, EValidatorResult Validity)
	{
		FString Text = INameValidatorInterface::GetErrorText(Name, Validity == EValidatorResult::Ok ? EValidatorResult::AlreadyInUse : Validity).BuildSourceString();
		Text.RemoveFromEnd(TEXT("."));
		return Text;
	}

	/** Type of a variable or parameter without a container. Returns the problem, empty when the type was found. */
	FString ParseTerminalType(FString Text, FEdGraphPinType& OutType)
	{
		OutType = FEdGraphPinType();
		const FString Written = Text.TrimStartAndEnd();
		// The pin types that blueprint_get_graph prints (UEdGraphSchema_K2::TypeToText): Linear Color Structure, Actor Object Reference.
		Text = Written.Replace(TEXT("(single-precision)"), TEXT("")).Replace(TEXT("(double-precision)"), TEXT(""));
		const bool bClassReference = Text.RemoveFromStart(TEXT("Class of ")) || Text.RemoveFromEnd(TEXT(" Class Reference"));
		if (!bClassReference && !Text.RemoveFromEnd(TEXT(" Object Reference")) && !Text.RemoveFromEnd(TEXT(" Structure")))
		{
			Text.RemoveFromEnd(TEXT(" Enum"));
		}
		Text.ReplaceInline(TEXT(" "), TEXT(""));
		if (Text.IsEmpty())
		{
			return TEXT("the type is empty");
		}

		static const TMap<FString, FName> BasicTypes = {
			{ TEXT("Boolean"), UEdGraphSchema_K2::PC_Boolean }, { TEXT("Bool"), UEdGraphSchema_K2::PC_Boolean },
			{ TEXT("Byte"), UEdGraphSchema_K2::PC_Byte },
			{ TEXT("Integer"), UEdGraphSchema_K2::PC_Int }, { TEXT("Int"), UEdGraphSchema_K2::PC_Int },
			{ TEXT("Integer64"), UEdGraphSchema_K2::PC_Int64 }, { TEXT("Int64"), UEdGraphSchema_K2::PC_Int64 },
			{ TEXT("Float"), UEdGraphSchema_K2::PC_Real }, { TEXT("Double"), UEdGraphSchema_K2::PC_Real }, { TEXT("Real"), UEdGraphSchema_K2::PC_Real },
			{ TEXT("Name"), UEdGraphSchema_K2::PC_Name },
			{ TEXT("String"), UEdGraphSchema_K2::PC_String },
			{ TEXT("Text"), UEdGraphSchema_K2::PC_Text },
		};
		if (const FName* Category = bClassReference ? nullptr : BasicTypes.Find(Text))
		{
			OutType.PinCategory = *Category;
			// Blueprint float variables are double precision (UBlueprintEditorLibrary::GetBasicTypeByName).
			OutType.PinSubCategory = *Category == UEdGraphSchema_K2::PC_Real ? UEdGraphSchema_K2::PC_Double : NAME_None;
			return FString();
		}

		if (!bClassReference)
		{
			if (UScriptStruct* Struct = Tools::ResolveStruct(Text))
			{
				if (!UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Struct))
				{
					return FString::Printf(TEXT("the struct %s cannot be used in Blueprints"), *Struct->GetName());
				}
				OutType.PinCategory = UEdGraphSchema_K2::PC_Struct;
				OutType.PinSubCategoryObject = Struct;
				return FString();
			}
			UEnum* Enum = Text.StartsWith(TEXT("/")) ? LoadObject<UEnum>(nullptr, *Text, nullptr, LOAD_NoWarn | LOAD_Quiet)
				: FindFirstObject<UEnum>(*Text, EFindFirstObjectOptions::NativeFirst);
			if (Enum)
			{
				if (!UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Enum))
				{
					return FString::Printf(TEXT("the enum %s cannot be used in Blueprints"), *Enum->GetName());
				}
				OutType.PinCategory = UEdGraphSchema_K2::PC_Byte;
				OutType.PinSubCategoryObject = Enum;
				return FString();
			}
		}

		UClass* Class = ResolveClassName(Text);
		if (!Class)
		{
			return FString::Printf(TEXT("'%s' is no type: write Boolean, Byte, Integer, Integer64, Float, Name, String, Text, a struct, an enum, a class or Class of <class>"), *Written);
		}
		if (!UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Class))
		{
			return FString::Printf(TEXT("the class %s cannot be used in Blueprints"), *Class->GetName());
		}
		OutType.PinCategory = bClassReference ? UEdGraphSchema_K2::PC_Class : (Class->HasAnyClassFlags(CLASS_Interface) ? UEdGraphSchema_K2::PC_Interface : UEdGraphSchema_K2::PC_Object);
		OutType.PinSubCategoryObject = Class;
		return FString();
	}

	/** Type of a variable or parameter, with Array of, Set of or Map of <key> to <value>. Returns the problem, empty when the type was found. */
	FString ParsePinType(const FString& Text, FEdGraphPinType& OutType)
	{
		FString Element = Text.TrimStartAndEnd();
		FString ValueText;
		EPinContainerType Container = EPinContainerType::None;
		if (Element.RemoveFromStart(TEXT("Array of ")))
		{
			Container = EPinContainerType::Array;
		}
		else if (Element.RemoveFromStart(TEXT("Set of ")))
		{
			Container = EPinContainerType::Set;
		}
		else if (Element.RemoveFromStart(TEXT("Map of ")))
		{
			if (!Element.Split(TEXT(" to "), &Element, &ValueText))
			{
				return TEXT("write a map type as Map of <key> to <value>");
			}
			Container = EPinContainerType::Map;
		}
		if (Container == EPinContainerType::None)
		{
			return ParseTerminalType(Element, OutType);
		}

		// TypeToText writes the element types of containers in the plural: Array of Integers.
		const auto ParseElement = [](const FString& Name, FEdGraphPinType& OutElement)
		{
			const FString Problem = ParseTerminalType(Name, OutElement);
			return !Problem.IsEmpty() && Name.EndsWith(TEXT("s")) && ParseTerminalType(Name.LeftChop(1), OutElement).IsEmpty() ? FString() : Problem;
		};
		FString Problem = ParseElement(Element, OutType);
		if (Problem.IsEmpty() && Container == EPinContainerType::Map)
		{
			FEdGraphPinType ValueType;
			Problem = ParseElement(ValueText, ValueType);
			OutType.PinValueType = FEdGraphTerminalType::FromPinType(ValueType);
		}
		OutType.ContainerType = Container;
		return Problem;
	}

	/** Problem with the default value of a new variable, or empty. */
	FString GetDefaultValueProblem(const FEdGraphPinType& Type, const FString& Name, const FString& Value)
	{
		if (Value.IsEmpty())
		{
			return FString();
		}
		if (Type.IsContainer())
		{
			return TEXT("containers take no default value here; set it on the class defaults with object_set_properties after blueprint_compile");
		}
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		FString UseValue;
		TObjectPtr<UObject> UseObject;
		FText UseText;
		Schema->GetPinDefaultValuesFromString(Type, nullptr, Value, UseValue, UseObject, UseText, /*bPreserveTextIdentity=*/false);
		FString Message;
		return Schema->DefaultValueSimpleValidation(Type, FName(*Name), UseValue, UseObject, UseText, &Message) ? FString() : Message;
	}
}

FAgentMcpGraphDetails UAgentMcpBlueprintTools::GetGraph(UBlueprint* Blueprint, const FString& Graph, const FString& Title, bool bEntryPointsOnly, const FString& ConnectedTo, int32 MaxNodes)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::BlueprintGraphToolsPrivate;

	FAgentMcpGraphDetails Result;
	if (!Tools::RequireObject(Blueprint, TEXT("blueprint")))
	{
		return Result;
	}
	UEdGraph* EdGraph = FindGraph(Blueprint, Graph);
	if (!EdGraph)
	{
		return Result;
	}

	TSet<const UEdGraphNode*> Connected;
	if (!ConnectedTo.IsEmpty())
	{
		const UEdGraphNode* Start = FindNode(EdGraph, ConnectedTo);
		if (!Start)
		{
			RaiseToolError(TEXT("NOT_FOUND"), FString::Printf(TEXT("%s has no node '%s'."), *EdGraph->GetName(), *ConnectedTo));
			return Result;
		}
		TArray<const UEdGraphNode*> Pending = { Start };
		while (!Pending.IsEmpty())
		{
			const UEdGraphNode* Node = Pending.Pop();
			if (Connected.Contains(Node))
			{
				continue;
			}
			Connected.Add(Node);
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin)
				{
					continue;
				}
				for (const UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (Linked && Linked->GetOwningNodeUnchecked())
					{
						Pending.Add(Linked->GetOwningNodeUnchecked());
					}
				}
			}
		}
	}

	Result.Blueprint = Blueprint->GetPathName();
	Result.Graph = EdGraph->GetName();
	Result.TotalNodes = EdGraph->Nodes.Num();
	const int32 Limit = FMath::Clamp(MaxNodes, 1, MaxGraphNodes);
	for (const UEdGraphNode* Node : EdGraph->Nodes)
	{
		if (!Node
			|| (!ConnectedTo.IsEmpty() && !Connected.Contains(Node))
			|| (bEntryPointsOnly && !IsEntryPoint(*Node))
			|| !Tools::MatchesNameFilter(GetNodeTitle(*Node), Title))
		{
			continue;
		}
		if (Result.Nodes.Num() >= Limit)
		{
			Result.bTruncated = true;
			break;
		}
		Result.Nodes.Add(MakeNode(*Node));
	}
	return Result;
}

FAgentMcpNodeTypeList UAgentMcpBlueprintTools::FindNodeTypes(UBlueprint* Blueprint, const FString& Graph, const FString& Filter, const FString& ContextPin, int32 MaxItems)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::BlueprintGraphToolsPrivate;

	FAgentMcpNodeTypeList Result;
	if (!Tools::RequireObject(Blueprint, TEXT("blueprint")))
	{
		return Result;
	}
	UEdGraph* EdGraph = FindGraph(Blueprint, Graph);
	if (!EdGraph)
	{
		return Result;
	}
	UEdGraphPin* Pin = nullptr;
	if (!ContextPin.IsEmpty())
	{
		FString Problem;
		Pin = FindPin(EdGraph, ContextPin, EGPD_Output, {}, Problem);
		if (!Pin)
		{
			RaiseToolError(TEXT("NOT_FOUND"), Problem + TEXT("."));
			return Result;
		}
	}

	const FString Pattern = Filter.Replace(TEXT(" "), TEXT(""));
	TArray<FAgentMcpNodeType> Found;
	TMap<FString, TSet<const UClass*>> ClassesByTypeId;
	ForEachNodeType(Blueprint, EdGraph, Pin,
		[&Pattern](const FString& TypeId) { return Tools::MatchesNameFilter(TypeId, Pattern); },
		[&Found, &ClassesByTypeId](FBlueprintActionInfo& Action, const FString& TypeId, const FBlueprintActionUiSpec& UiSpec)
		{
			const UClass* DeclaringClass = GetAuthoritativeClass(Action.GetOwnerClass());
			TSet<const UClass*>& Classes = ClassesByTypeId.FindOrAdd(TypeId);
			if (Classes.Contains(DeclaringClass))
			{
				return;
			}
			Classes.Add(DeclaringClass);
			FAgentMcpNodeType& Entry = Found.AddDefaulted_GetRef();
			Entry.TypeId = TypeId;
			Entry.DeclaringClass = GetPathNameSafe(DeclaringClass);
			Entry.Description = Tools::GetShortDescription(UiSpec.Tooltip.BuildSourceString(), 160);
		});

	// A short filter such as Sin or Add matches many ids (Sensing, Address), so the node types it names come first.
	const bool bWildcard = Pattern.Contains(TEXT("*")) || Pattern.Contains(TEXT("?"));
	Found.Sort([&Pattern, bWildcard](const FAgentMcpNodeType& A, const FAgentMcpNodeType& B)
	{
		const int32 RankA = bWildcard ? 0 : GetMatchRank(A.TypeId, Pattern);
		const int32 RankB = bWildcard ? 0 : GetMatchRank(B.TypeId, Pattern);
		return RankA != RankB ? RankA < RankB : A.TypeId < B.TypeId;
	});
	const int32 Limit = FMath::Clamp(MaxItems, 1, MaxNodeTypes);
	for (FAgentMcpNodeType& Entry : Found)
	{
		if (Result.NodeTypes.Num() >= Limit)
		{
			Result.bTruncated = true;
			break;
		}
		// The declaring class only matters where it tells node types of one type id apart.
		if (ClassesByTypeId.FindChecked(Entry.TypeId).Num() < 2)
		{
			Entry.DeclaringClass.Reset();
		}
		Result.NodeTypes.Add(MoveTemp(Entry));
	}
	return Result;
}

FAgentMcpGraphEditResult UAgentMcpBlueprintTools::EditGraph(UBlueprint* Blueprint, const FString& Graph, const TArray<FAgentMcpGraphEdit>& Operations)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::BlueprintGraphToolsPrivate;

	FAgentMcpGraphEditResult Result;
	if (!Tools::RequireObject(Blueprint, TEXT("blueprint")) || !Tools::RequireProjectContent(Blueprint))
	{
		return Result;
	}
	UEdGraph* EdGraph = FindGraph(Blueprint, Graph);
	if (!EdGraph)
	{
		return Result;
	}
	const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(EdGraph->GetSchema());
	if (!Schema)
	{
		RaiseToolError(TEXT("NOT_SUPPORTED"), FString::Printf(TEXT("%s is not a Blueprint script graph."), *EdGraph->GetName()));
		return Result;
	}
	if (Operations.IsEmpty())
	{
		RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("'operations' is empty."));
		return Result;
	}

	const UEnum* OpEnum = StaticEnum<EAgentMcpGraphEditOp>();
	const auto Describe = [OpEnum, &Operations](int32 Index)
	{
		return FString::Printf(TEXT("operation %d (%s)"), Index, *OpEnum->GetNameStringByValue(static_cast<int64>(Operations[Index].Op)));
	};

	// Check every operation before anything changes: fields, refs, node names and node types.
	// Node types are checked after the other fields, so problems are sorted by operation afterwards.
	TArray<TPair<int32, FString>> Problems;
	TSet<FString> ProblemCodes;
	const auto AddProblem = [&Problems, &ProblemCodes, &Describe](int32 Index, const FString& Text, const TCHAR* Code = TEXT("INVALID_ARGUMENT"))
	{
		Problems.Emplace(Index, FString::Printf(TEXT("%s: %s"), *Describe(Index), *Text));
		ProblemCodes.Add(Code);
	};
	TSet<FString> Refs;
	const auto CheckNode = [&](int32 Index, const FString& NodeName, const TCHAR* Field)
	{
		if (NodeName.IsEmpty())
		{
			AddProblem(Index, FString::Printf(TEXT("'%s' is required"), Field));
		}
		else if (!Refs.Contains(NodeName) && !FindNode(EdGraph, NodeName))
		{
			AddProblem(Index, FString::Printf(TEXT("no node or earlier ref '%s'"), *NodeName), TEXT("NOT_FOUND"));
		}
	};
	const auto CheckPin = [&](int32 Index, const FString& PinPath, const TCHAR* Field)
	{
		FString NodeName;
		FString PinName;
		if (!PinPath.Split(TEXT("."), &NodeName, &PinName) || NodeName.IsEmpty() || PinName.IsEmpty())
		{
			AddProblem(Index, FString::Printf(TEXT("'%s' must be Node.Pin"), Field));
			return;
		}
		CheckNode(Index, NodeName, Field);
	};

	TMap<int32, FResolvedType> TypesByOperation;
	TSet<FString> NewEventNames;
	for (int32 Index = 0; Index < Operations.Num(); ++Index)
	{
		const FAgentMcpGraphEdit& Edit = Operations[Index];
		switch (Edit.Op)
		{
		case EAgentMcpGraphEditOp::Add:
			if (Edit.TypeId.IsEmpty())
			{
				AddProblem(Index, TEXT("'typeId' is required"));
			}
			else if (Edit.TypeId.StartsWith(CustomEventPrefix))
			{
				const FString EventName = Edit.TypeId.RightChop(CustomEventPrefix.Len());
				const EValidatorResult Validity = FKismetNameValidator(Blueprint).IsValid(EventName);
				if (!FBlueprintEditorUtils::IsEventGraph(EdGraph))
				{
					AddProblem(Index, TEXT("custom events go into an event graph"), TEXT("NOT_SUPPORTED"));
				}
				else if (Validity != EValidatorResult::Ok || NewEventNames.Contains(EventName))
				{
					AddProblem(Index, FString::Printf(TEXT("'%s' cannot name a new event: %s"), *EventName,
						*GetNameProblem(EventName, Validity)));
				}
				NewEventNames.Add(EventName);
			}
			else
			{
				TypesByOperation.Add(Index);
			}
			if (!Edit.Ref.IsEmpty())
			{
				if (Edit.Ref.Contains(TEXT(".")))
				{
					AddProblem(Index, FString::Printf(TEXT("ref '%s' contains a period"), *Edit.Ref));
				}
				else if (Refs.Contains(Edit.Ref) || FindNode(EdGraph, Edit.Ref))
				{
					AddProblem(Index, FString::Printf(TEXT("ref '%s' is already a ref or a node name"), *Edit.Ref));
				}
				Refs.Add(Edit.Ref);
			}
			break;
		case EAgentMcpGraphEditOp::Remove:
		case EAgentMcpGraphEditOp::AddPin:
		case EAgentMcpGraphEditOp::Move:
			CheckNode(Index, Edit.Node, TEXT("node"));
			break;
		case EAgentMcpGraphEditOp::Connect:
		case EAgentMcpGraphEditOp::Break:
			CheckPin(Index, Edit.From, TEXT("from"));
			CheckPin(Index, Edit.To, TEXT("to"));
			break;
		case EAgentMcpGraphEditOp::SetDefault:
			CheckPin(Index, Edit.From, TEXT("from"));
			break;
		}
	}

	if (!TypesByOperation.IsEmpty())
	{
		TMap<int32, const UClass*> RequestedClasses;
		TSet<FString> TypeIds;
		for (const TPair<int32, FResolvedType>& Entry : TypesByOperation)
		{
			const FAgentMcpGraphEdit& Edit = Operations[Entry.Key];
			TypeIds.Add(Edit.TypeId);
			if (!Edit.DeclaringClass.IsEmpty())
			{
				const UClass* Class = ResolveClassName(Edit.DeclaringClass);
				if (!Class)
				{
					AddProblem(Entry.Key, FString::Printf(TEXT("no class '%s'"), *Edit.DeclaringClass), TEXT("NOT_FOUND"));
				}
				RequestedClasses.Add(Entry.Key, GetAuthoritativeClass(Class));
			}
		}

		// Type ids compare case-insensitively (FString), as UE 5.8 canonicalizes them.
		TMap<FString, TArray<FResolvedType>> Candidates;
		ForEachNodeType(Blueprint, EdGraph, nullptr,
			[&TypeIds](const FString& TypeId) { return TypeIds.Contains(TypeId); },
			[&Candidates](FBlueprintActionInfo& Action, const FString& TypeId, const FBlueprintActionUiSpec&)
			{
				Candidates.FindOrAdd(TypeId).Add({ Action.NodeSpawner, GetAuthoritativeClass(Action.GetOwnerClass()) });
			});

		for (TPair<int32, FResolvedType>& Entry : TypesByOperation)
		{
			const FAgentMcpGraphEdit& Edit = Operations[Entry.Key];
			const UClass* const* Requested = RequestedClasses.Find(Entry.Key);
			TArray<FResolvedType> Matches;
			TArray<FString> ClassNames;
			if (const TArray<FResolvedType>* Found = Candidates.Find(Edit.TypeId))
			{
				for (const FResolvedType& Candidate : *Found)
				{
					const bool bRequested = !Requested || Candidate.DeclaringClass == *Requested;
					if (bRequested && !Matches.ContainsByPredicate([&Candidate](const FResolvedType& Match) { return Match.DeclaringClass == Candidate.DeclaringClass; }))
					{
						Matches.Add(Candidate);
						ClassNames.Add(GetPathNameSafe(Candidate.DeclaringClass));
					}
				}
			}
			if (Matches.IsEmpty())
			{
				AddProblem(Entry.Key, FString::Printf(TEXT("no node type '%s'%s can be added to %s"), *Edit.TypeId,
					Requested ? *FString::Printf(TEXT(" of %s"), *Edit.DeclaringClass) : TEXT(""), *EdGraph->GetName()), TEXT("NOT_FOUND"));
			}
			else if (Matches.Num() > 1)
			{
				AddProblem(Entry.Key, FString::Printf(TEXT("'%s' is declared by several classes, pass declaringClass: %s"), *Edit.TypeId, *FString::Join(ClassNames, TEXT(", "))), TEXT("AMBIGUOUS_REFERENCE"));
			}
			else
			{
				Entry.Value = Matches[0];
			}
		}
	}

	if (!Problems.IsEmpty())
	{
		Problems.StableSort([](const TPair<int32, FString>& A, const TPair<int32, FString>& B) { return A.Key < B.Key; });
		TArray<FString> Texts;
		for (const TPair<int32, FString>& Problem : Problems)
		{
			Texts.Add(Problem.Value);
		}
		Tools::RaiseProblems(TEXT("The graph was not changed"), Texts, ProblemCodes, TEXT("Look node types up with blueprint_find_node_types and node names with blueprint_get_graph."));
		return Result;
	}

	// Apply. The tool runs in one transaction, and a failing operation undoes the operations before it (AgentMcpReflectedTool).
	const auto Fail = [&Describe](int32 Index, const FString& Text, const TCHAR* Code = TEXT("GRAPH_EDIT_FAILED"))
	{
		RaiseToolError(Code, FString::Printf(TEXT("%s: %s. The graph was not changed."), *Describe(Index), *Text));
	};
	TMap<FString, UEdGraphNode*> RefNodes;
	const auto ResolveNode = [&RefNodes, EdGraph](const FString& Name) -> UEdGraphNode*
	{
		UEdGraphNode* const* RefNode = RefNodes.Find(Name);
		UEdGraphNode* Node = RefNode ? *RefNode : FindNode(EdGraph, Name);
		return Node && EdGraph->Nodes.Contains(Node) ? Node : nullptr;
	};

	TSet<const UEdGraphNode*> NodesBefore;
	for (const UEdGraphNode* Node : EdGraph->Nodes)
	{
		NodesBefore.Add(Node);
	}
	TSet<const UEdGraphNode*> Touched;
	bool bStructural = false;
	const auto NoteStructural = [&bStructural](const UEdGraphNode* Node)
	{
		const UK2Node* K2Node = Cast<UK2Node>(Node);
		bStructural |= !K2Node || K2Node->NodeCausesStructuralBlueprintChange();
	};

	EdGraph->Modify();
	for (int32 Index = 0; Index < Operations.Num(); ++Index)
	{
		const FAgentMcpGraphEdit& Edit = Operations[Index];
		switch (Edit.Op)
		{
		case EAgentMcpGraphEditOp::Add:
		{
			const int32 NodeCountBefore = EdGraph->Nodes.Num();
			UEdGraphNode* Node = nullptr;
			if (const FResolvedType* Type = TypesByOperation.Find(Index))
			{
				// May return the event node that the graph already has.
				Node = Type->Spawner->Invoke(EdGraph, IBlueprintNodeBinder::FBindingSet(), FVector2D(Edit.X, Edit.Y));
			}
			else
			{
				// UE 5.8 UBlueprintGraphEditor::AddCustomEventNode.
				UK2Node_CustomEvent* Event = NewObject<UK2Node_CustomEvent>(EdGraph);
				Event->CreateNewGuid();
				Event->NodePosX = Edit.X;
				Event->NodePosY = Edit.Y;
				Event->CustomFunctionName = FName(*Edit.TypeId.RightChop(CustomEventPrefix.Len()));
				Event->bIsEditable = true;
				Event->SetFlags(RF_Transactional);
				Event->AllocateDefaultPins();
				Event->PostPlacedNewNode();
				EdGraph->AddNode(Event, /*bFromUI=*/true, /*bSelectNewNode=*/false);
				Node = Event;
			}
			if (!Node)
			{
				Fail(Index, FString::Printf(TEXT("could not add '%s'"), *Edit.TypeId));
				return Result;
			}
			if (EdGraph->Nodes.Num() > NodeCountBefore)
			{
				NoteStructural(Node);
			}
			if (!Edit.Ref.IsEmpty())
			{
				RefNodes.Add(Edit.Ref, Node);
				Result.Refs.Add(Edit.Ref, Node->GetName());
			}
			Touched.Add(Node);
			break;
		}
		case EAgentMcpGraphEditOp::Remove:
		{
			UEdGraphNode* Node = ResolveNode(Edit.Node);
			if (!Node)
			{
				Fail(Index, FString::Printf(TEXT("node '%s' was removed earlier"), *Edit.Node), TEXT("NOT_FOUND"));
				return Result;
			}
			if (!Node->CanUserDeleteNode())
			{
				Fail(Index, FString::Printf(TEXT("%s cannot be deleted"), *Node->GetName()), TEXT("NOT_SUPPORTED"));
				return Result;
			}
			NoteStructural(Node);
			FBlueprintEditorUtils::RemoveNode(Blueprint, Node, /*bDontRecompile=*/true);
			break;
		}
		case EAgentMcpGraphEditOp::Connect:
		case EAgentMcpGraphEditOp::Break:
		{
			FString Problem;
			UEdGraphPin* From = FindPin(EdGraph, Edit.From, EGPD_Output, RefNodes, Problem);
			UEdGraphPin* To = From ? FindPin(EdGraph, Edit.To, EGPD_Input, RefNodes, Problem) : nullptr;
			if (!From || !To)
			{
				Fail(Index, Problem, TEXT("NOT_FOUND"));
				return Result;
			}
			UEdGraphNode* FromNode = From->GetOwningNode();
			UEdGraphNode* ToNode = To->GetOwningNode();
			FromNode->Modify();
			ToNode->Modify();
			if (Edit.Op == EAgentMcpGraphEditOp::Connect)
			{
				const FPinConnectionResponse Response = Schema->CanCreateConnection(From, To);
				if (Response.Response == CONNECT_RESPONSE_DISALLOW)
				{
					Fail(Index, FString::Printf(TEXT("%s cannot connect to %s: %s"), *Edit.From, *Edit.To, *Response.Message.BuildSourceString()), TEXT("INVALID_ARGUMENT"));
					return Result;
				}
				if (!Schema->TryCreateConnection(From, To))
				{
					Fail(Index, FString::Printf(TEXT("could not connect %s to %s"), *Edit.From, *Edit.To));
					return Result;
				}
			}
			else
			{
				if (!From->LinkedTo.Contains(To))
				{
					Fail(Index, FString::Printf(TEXT("%s is not linked to %s"), *Edit.From, *Edit.To), TEXT("INVALID_ARGUMENT"));
					return Result;
				}
				Schema->BreakSinglePinLink(From, To);
			}
			Touched.Add(FromNode);
			Touched.Add(ToNode);
			break;
		}
		case EAgentMcpGraphEditOp::SetDefault:
		{
			FString Problem;
			UEdGraphPin* Pin = FindPin(EdGraph, Edit.From, EGPD_Input, RefNodes, Problem);
			if (!Pin)
			{
				Fail(Index, Problem, TEXT("NOT_FOUND"));
				return Result;
			}
			if (Pin->Direction != EGPD_Input || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec || Pin->bDefaultValueIsIgnored)
			{
				Fail(Index, FString::Printf(TEXT("%s takes no value"), *Edit.From), TEXT("INVALID_ARGUMENT"));
				return Result;
			}
			if (!Pin->LinkedTo.IsEmpty())
			{
				Fail(Index, FString::Printf(TEXT("%s is connected, so its value is not used; break the link first"), *Edit.From), TEXT("INVALID_ARGUMENT"));
				return Result;
			}
			// TrySetDefaultValue ignores invalid values silently, so check the value the same way first.
			FString UseValue;
			TObjectPtr<UObject> UseObject;
			FText UseText;
			Schema->GetPinDefaultValuesFromString(Pin->PinType, Pin->GetOwningNodeUnchecked(), Edit.Value, UseValue, UseObject, UseText, /*bPreserveTextIdentity=*/false);
			Problem = Schema->IsPinDefaultValid(Pin, UseValue, UseObject, UseText);
			if (!Problem.IsEmpty())
			{
				Fail(Index, FString::Printf(TEXT("'%s' is not a value of %s: %s"), *Edit.Value, *Edit.From, *Problem), TEXT("INVALID_ARGUMENT"));
				return Result;
			}
			Pin->GetOwningNode()->Modify();
			Schema->TrySetDefaultValue(*Pin, Edit.Value, /*bMarkAsModified=*/false);
			Touched.Add(Pin->GetOwningNode());
			break;
		}
		case EAgentMcpGraphEditOp::AddPin:
		{
			UK2Node* Node = Cast<UK2Node>(ResolveNode(Edit.Node));
			IK2Node_AddPinInterface* AddPinNode = Cast<IK2Node_AddPinInterface>(Node);
			UK2Node_Switch* SwitchNode = Cast<UK2Node_Switch>(Node);
			// UE 5.8 UBlueprintGraphEditor::AddNodePin: switches have their own method, other nodes the add-pin interface.
			if (!Node || (!SwitchNode && !AddPinNode) || (AddPinNode && !AddPinNode->CanAddPin()))
			{
				Fail(Index, FString::Printf(TEXT("%s takes no more pins"), *Edit.Node), TEXT("NOT_SUPPORTED"));
				return Result;
			}
			Node->Modify();
			if (SwitchNode)
			{
				SwitchNode->AddPinToSwitchNode();
			}
			else
			{
				AddPinNode->AddInputPin();
			}
			NoteStructural(Node);
			Touched.Add(Node);
			break;
		}
		case EAgentMcpGraphEditOp::Move:
		{
			UEdGraphNode* Node = ResolveNode(Edit.Node);
			if (!Node)
			{
				Fail(Index, FString::Printf(TEXT("node '%s' was removed earlier"), *Edit.Node), TEXT("NOT_FOUND"));
				return Result;
			}
			Node->Modify();
			Node->NodePosX = Edit.X;
			Node->NodePosY = Edit.Y;
			Touched.Add(Node);
			break;
		}
		}
	}

	if (bStructural)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	else
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}

	Result.Blueprint = Blueprint->GetPathName();
	Result.Graph = EdGraph->GetName();
	for (const UEdGraphNode* Node : EdGraph->Nodes)
	{
		if (Node && (Touched.Contains(Node) || !NodesBefore.Contains(Node)))
		{
			Result.Nodes.Add(MakeNode(*Node));
		}
	}
	return Result;
}

FAgentMcpBlueprintMembersResult UAgentMcpBlueprintTools::AddMembers(UBlueprint* Blueprint, const TArray<FAgentMcpNewVariable>& Variables, const TArray<FAgentMcpNewFunction>& Functions,
	const TArray<FAgentMcpNewEventDispatcher>& EventDispatchers)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::BlueprintGraphToolsPrivate;

	FAgentMcpBlueprintMembersResult Result;
	if (!Tools::RequireObject(Blueprint, TEXT("blueprint")) || !Tools::RequireProjectContent(Blueprint))
	{
		return Result;
	}
	if (Variables.IsEmpty() && Functions.IsEmpty() && EventDispatchers.IsEmpty())
	{
		RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("Nothing to add: 'variables', 'functions' and 'eventDispatchers' are empty."));
		return Result;
	}

	// Check every name, type and default value before anything changes.
	TArray<FString> Problems;
	TSet<FString> ProblemCodes;
	TSet<FString> NewNames;
	const auto AddProblem = [&Problems, &ProblemCodes](const FString& Where, const FString& Text)
	{
		Problems.Add(FString::Printf(TEXT("%s: %s"), *Where, *Text));
		ProblemCodes.Add(TEXT("INVALID_ARGUMENT"));
	};
	const auto CheckName = [&](const FString& Where, const FString& Name)
	{
		const EValidatorResult Validity = Name.IsEmpty() ? EValidatorResult::EmptyName : FKismetNameValidator(Blueprint).IsValid(Name);
		if (Validity != EValidatorResult::Ok || NewNames.Contains(Name))
		{
			AddProblem(Where, FString::Printf(TEXT("'%s' cannot name a new member: %s"), *Name,
				*GetNameProblem(Name, Validity)));
		}
		NewNames.Add(Name);
	};
	const auto CheckType = [&](const FString& Where, const FString& Type, FEdGraphPinType& OutType)
	{
		const FString Problem = ParsePinType(Type, OutType);
		if (!Problem.IsEmpty())
		{
			AddProblem(Where, Problem);
		}
	};
	const auto CheckParameters = [&](const FString& Where, const TArray<FAgentMcpNewParameter>& Inputs, const TArray<FAgentMcpNewParameter>& Outputs,
		TArray<FEdGraphPinType>& OutInputTypes, TArray<FEdGraphPinType>& OutOutputTypes)
	{
		TSet<FString> ParameterNames;
		for (const bool bInputs : { true, false })
		{
			const TArray<FAgentMcpNewParameter>& List = bInputs ? Inputs : Outputs;
			TArray<FEdGraphPinType>& Types = bInputs ? OutInputTypes : OutOutputTypes;
			for (int32 Index = 0; Index < List.Num(); ++Index)
			{
				const FString ParameterWhere = FString::Printf(TEXT("%s.%s[%d]"), *Where, bInputs ? TEXT("inputs") : TEXT("outputs"), Index);
				if (List[Index].Name.IsEmpty() || ParameterNames.Contains(List[Index].Name))
				{
					AddProblem(ParameterWhere, TEXT("parameter names must be set and differ"));
				}
				ParameterNames.Add(List[Index].Name);
				CheckType(ParameterWhere, List[Index].Type, Types.AddDefaulted_GetRef());
			}
		}
	};

	TArray<FEdGraphPinType> VariableTypes;
	for (int32 Index = 0; Index < Variables.Num(); ++Index)
	{
		const FAgentMcpNewVariable& Variable = Variables[Index];
		const FString Where = FString::Printf(TEXT("variables[%d]"), Index);
		CheckName(Where, Variable.Name);
		FEdGraphPinType& Type = VariableTypes.AddDefaulted_GetRef();
		CheckType(Where, Variable.Type, Type);
		const FString Problem = Type.PinCategory.IsNone() ? FString() : GetDefaultValueProblem(Type, Variable.Name, Variable.DefaultValue);
		if (!Problem.IsEmpty())
		{
			AddProblem(Where, FString::Printf(TEXT("'%s' is not a default value of %s: %s"), *Variable.DefaultValue, *Variable.Type, *Problem));
		}
	}
	TArray<TArray<FEdGraphPinType>> FunctionInputTypes;
	TArray<TArray<FEdGraphPinType>> FunctionOutputTypes;
	for (int32 Index = 0; Index < Functions.Num(); ++Index)
	{
		const FString Where = FString::Printf(TEXT("functions[%d]"), Index);
		CheckName(Where, Functions[Index].Name);
		CheckParameters(Where, Functions[Index].Inputs, Functions[Index].Outputs, FunctionInputTypes.AddDefaulted_GetRef(), FunctionOutputTypes.AddDefaulted_GetRef());
	}
	TArray<TArray<FEdGraphPinType>> DispatcherInputTypes;
	for (int32 Index = 0; Index < EventDispatchers.Num(); ++Index)
	{
		const FString Where = FString::Printf(TEXT("eventDispatchers[%d]"), Index);
		CheckName(Where, EventDispatchers[Index].Name);
		TArray<FEdGraphPinType> NoOutputTypes;
		CheckParameters(Where, EventDispatchers[Index].Inputs, {}, DispatcherInputTypes.AddDefaulted_GetRef(), NoOutputTypes);
	}
	if (!Problems.IsEmpty())
	{
		Tools::RaiseProblems(TEXT("Nothing was added"), Problems, ProblemCodes, TEXT("blueprint_inspect lists the members the Blueprint already has."));
		return Result;
	}

	// Apply as the My Blueprint panel does. The tool runs in one transaction, and a failure undoes the whole call (AgentMcpReflectedTool).
	const auto Fail = [](const FString& Where, const FString& Text)
	{
		RaiseToolError(TEXT("MEMBER_ADD_FAILED"), FString::Printf(TEXT("%s: %s. Nothing was added."), *Where, *Text), TEXT("log_get_recent may show the reason."));
	};
	Blueprint->Modify();
	for (int32 Index = 0; Index < Variables.Num(); ++Index)
	{
		const FAgentMcpNewVariable& Variable = Variables[Index];
		const FName Name(*Variable.Name);
		if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, Name, VariableTypes[Index], Variable.DefaultValue))
		{
			Fail(FString::Printf(TEXT("variables[%d]"), Index), FString::Printf(TEXT("%s could not be added"), *Variable.Name));
			return Result;
		}
		if (!Variable.Category.IsEmpty())
		{
			FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, Name, nullptr, FText::FromString(Variable.Category), /*bDontRecompile=*/true);
		}
		if (Variable.bInstanceEditable)
		{
			FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, Name, /*bNewBlueprintOnly=*/false);
		}
		if (Variable.bExposeOnSpawn)
		{
			FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, Name, nullptr, FBlueprintMetadata::MD_ExposeOnSpawn, TEXT("true"));
		}
		Result.Variables.Add(Variable.Name);
	}

	TArray<UEdGraph*> FunctionGraphs;
	for (int32 Index = 0; Index < Functions.Num(); ++Index)
	{
		const FAgentMcpNewFunction& Function = Functions[Index];
		const FString Where = FString::Printf(TEXT("functions[%d]"), Index);
		// UE 5.8 UBlueprintEditorLibrary::AddFunctionGraph, UBlueprintGraphEditor::AddGraphInputParameter and AddGraphOutputParameter.
		UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(*Function.Name), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		FBlueprintEditorUtils::AddFunctionGraph<UFunction>(Blueprint, Graph, /*bIsUserCreated=*/true, nullptr);
		TArray<UK2Node_FunctionEntry*> Entries;
		Graph->GetNodesOfClass(Entries);
		if (Entries.IsEmpty())
		{
			Fail(Where, FString::Printf(TEXT("%s has no entry node"), *Function.Name));
			return Result;
		}
		UK2Node_FunctionEntry* Entry = Entries[0];
		Entry->Modify();
		for (int32 Input = 0; Input < Function.Inputs.Num(); ++Input)
		{
			Entry->CreateUserDefinedPin(FName(*Function.Inputs[Input].Name), FunctionInputTypes[Index][Input], EGPD_Output);
		}
		if (Function.bPure)
		{
			Entry->AddExtraFlags(FUNC_BlueprintPure);
		}
		if (!Function.Outputs.IsEmpty())
		{
			TArray<UK2Node_FunctionResult*> Returns;
			Graph->GetNodesOfClass(Returns);
			UK2Node_FunctionResult* Return = !Returns.IsEmpty() ? Returns[0]
				: Cast<UK2Node_FunctionResult>(UBlueprintNodeSpawner::Create(UK2Node_FunctionResult::StaticClass())->Invoke(Graph, IBlueprintNodeBinder::FBindingSet(), FVector2D(Entry->NodePosX + 600, Entry->NodePosY)));
			if (!Return)
			{
				Fail(Where, FString::Printf(TEXT("%s got no return node"), *Function.Name));
				return Result;
			}
			Return->Modify();
			for (int32 Output = 0; Output < Function.Outputs.Num(); ++Output)
			{
				Return->CreateUserDefinedPin(FName(*Function.Outputs[Output].Name), FunctionOutputTypes[Index][Output], EGPD_Input);
			}
		}
		FunctionGraphs.Add(Graph);
	}

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	for (int32 Index = 0; Index < EventDispatchers.Num(); ++Index)
	{
		const FAgentMcpNewEventDispatcher& Dispatcher = EventDispatchers[Index];
		const FName Name(*Dispatcher.Name);
		// UE 5.8 UBlueprintEditorLibrary::AddEventDispatcher and AddEventDispatcherParameter.
		FEdGraphPinType DelegateType;
		DelegateType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
		UEdGraph* Signature = FBlueprintEditorUtils::AddMemberVariable(Blueprint, Name, DelegateType)
			? FBlueprintEditorUtils::CreateNewGraph(Blueprint, Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass())
			: nullptr;
		if (!Signature)
		{
			Fail(FString::Printf(TEXT("eventDispatchers[%d]"), Index), FString::Printf(TEXT("%s could not be added"), *Dispatcher.Name));
			return Result;
		}
		Signature->bEditable = false;
		Schema->CreateDefaultNodesForGraph(*Signature);
		Schema->CreateFunctionGraphTerminators(*Signature, (UClass*)nullptr);
		Schema->AddExtraFunctionFlags(Signature, FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public);
		Schema->MarkFunctionEntryAsEditable(Signature, true);
		Blueprint->DelegateSignatureGraphs.Add(Signature);
		TArray<UK2Node_FunctionEntry*> Entries;
		Signature->GetNodesOfClass(Entries);
		for (int32 Input = 0; Input < Dispatcher.Inputs.Num() && !Entries.IsEmpty(); ++Input)
		{
			Entries[0]->Modify();
			Entries[0]->CreateUserDefinedPin(FName(*Dispatcher.Inputs[Input].Name), DispatcherInputTypes[Index][Input], EGPD_Output);
		}
		Result.EventDispatchers.Add(Dispatcher.Name);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	Result.Blueprint = Blueprint->GetPathName();
	for (const UEdGraph* Graph : FunctionGraphs)
	{
		FAgentMcpAddedFunction& Added = Result.Functions.AddDefaulted_GetRef();
		Added.Graph = Graph->GetName();
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (Cast<UK2Node_FunctionEntry>(Node) || Cast<UK2Node_FunctionResult>(Node))
			{
				Added.Nodes.Add(MakeNode(*Node));
			}
		}
	}
	return Result;
}

// blueprint_read_graph writes graphs as pseudo-code for reading, the way UE 5.8 read_graph_dsl decompiles them (blueprint_dsl.py
// Decompiler), but only one way: statements follow execution wires from each entry, and pure nodes become expressions.
namespace UE::AgentMcp::BlueprintGraphToolsPrivate
{
	constexpr int32 MaxExpressionDepth = 24;

	bool IsExecPin(const UEdGraphPin* Pin)
	{
		return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
	}

	bool HasExecPins(const UEdGraphNode& Node)
	{
		return Node.Pins.ContainsByPredicate([](const UEdGraphPin* Pin) { return IsExecPin(Pin); });
	}

	TArray<const UEdGraphPin*> GetDataPins(const UEdGraphNode& Node, EEdGraphPinDirection Direction)
	{
		TArray<const UEdGraphPin*> Result;
		for (const UEdGraphPin* Pin : Node.Pins)
		{
			if (Pin && !Pin->bHidden && Pin->Direction == Direction && !IsExecPin(Pin))
			{
				Result.Add(Pin);
			}
		}
		return Result;
	}

	/** Infix operators of the functions behind math and comparison nodes (Multiply_DoubleDouble is *). */
	const TCHAR* GetOperator(const FString& FunctionName)
	{
		static const TPair<const TCHAR*, const TCHAR*> Operators[] = {
			{ TEXT("Add_"), TEXT("+") }, { TEXT("Subtract_"), TEXT("-") }, { TEXT("Multiply_"), TEXT("*") }, { TEXT("Divide_"), TEXT("/") },
			{ TEXT("Percent_"), TEXT("%") }, { TEXT("LessEqual_"), TEXT("<=") }, { TEXT("Less_"), TEXT("<") },
			{ TEXT("GreaterEqual_"), TEXT(">=") }, { TEXT("Greater_"), TEXT(">") }, { TEXT("EqualEqual_"), TEXT("==") },
			{ TEXT("NotEqual_"), TEXT("!=") }, { TEXT("BooleanAND"), TEXT("&&") }, { TEXT("BooleanOR"), TEXT("||") },
		};
		for (const TPair<const TCHAR*, const TCHAR*>& Operator : Operators)
		{
			if (FunctionName.StartsWith(Operator.Key, ESearchCase::CaseSensitive))
			{
				return Operator.Value;
			}
		}
		return nullptr;
	}

	class FGraphWriter
	{
	public:
		FGraphWriter(const UEdGraph& InGraph, bool bInNodeNames)
			: Graph(InGraph), bNodeNames(bInNodeNames)
		{
		}

		FString Write()
		{
			for (const UEdGraphNode* Node : Graph.Nodes)
			{
				if (const UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node))
				{
					AddLine(0, TEXT("# note: ") + Comment->NodeComment.Replace(TEXT("\n"), TEXT(" ")));
				}
				// A node that several execution paths reach gets a label, so the text names it once.
				for (const UEdGraphPin* Pin : Node ? Node->Pins : TArray<UEdGraphPin*>())
				{
					if (IsExecPin(Pin) && Pin->Direction == EGPD_Input && Pin->LinkedTo.Num() > 1)
					{
						Labeled.Add(Node);
					}
				}
			}
			for (const UEdGraphNode* Node : Graph.Nodes)
			{
				if (Node && IsEntryPoint(*Node))
				{
					WriteEntry(*Node);
				}
			}

			TArray<FString> Unreached;
			for (const UEdGraphNode* Node : Graph.Nodes)
			{
				if (Node && HasExecPins(*Node) && !IsEntryPoint(*Node) && !Emitted.Contains(Node))
				{
					Unreached.Add(Node->GetName());
				}
			}
			if (!Unreached.IsEmpty())
			{
				AddLine(0, FString::Printf(TEXT("# not reached from an entry: %s"), *FString::Join(Unreached, TEXT(", "))));
			}
			return FString::Join(Lines, TEXT("\n"));
		}

	private:
		const UEdGraph& Graph;
		const bool bNodeNames;
		TArray<FString> Lines;
		TSet<const UEdGraphNode*> Emitted;
		TSet<const UEdGraphNode*> Labeled;
		TMap<const UEdGraphNode*, FString> Results;
		int32 ResultCount = 0;

		void AddLine(int32 Indent, const FString& Text)
		{
			Lines.Add(FString::ChrN(Indent * 2, TEXT(' ')) + Text);
		}

		static FString GetCallName(const UEdGraphNode& Node)
		{
			if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(&Node))
			{
				if (const UFunction* Function = Call->GetTargetFunction())
				{
					FString Name = Function->GetName();
					Name.RemoveFromStart(TEXT("K2_"));
					return Name;
				}
			}
			return GetNodeTitle(Node).Replace(TEXT(" "), TEXT(""));
		}

		FString Literal(const UEdGraphPin& Pin) const
		{
			const FString Value = Pin.GetDefaultAsString();
			const FName Category = Pin.PinType.PinCategory;
			const bool bQuoted = Category == UEdGraphSchema_K2::PC_String || Category == UEdGraphSchema_K2::PC_Text || Category == UEdGraphSchema_K2::PC_Name;
			return bQuoted ? TEXT("\"") + Value + TEXT("\"") : (Value.IsEmpty() ? FString(TEXT("none")) : Value);
		}

		/** The value an input pin receives: the expression of its link, or its own value. */
		FString Input(const UEdGraphPin& Pin, int32 Depth)
		{
			return Pin.LinkedTo.IsEmpty() || !Pin.LinkedTo[0] ? Literal(Pin) : Source(*Pin.LinkedTo[0], Depth);
		}

		/** Inputs of a call worth writing: linked ones, and values other than the pin's own default. The hidden self is left out. */
		FString Arguments(const UEdGraphNode& Node, int32 Depth)
		{
			TArray<FString> Arguments;
			for (const UEdGraphPin* Pin : GetDataPins(Node, EGPD_Input))
			{
				if (Pin->PinName == UEdGraphSchema_K2::PN_Self || (Pin->LinkedTo.IsEmpty() && Pin->DoesDefaultValueMatchAutogenerated()))
				{
					continue;
				}
				Arguments.Add(FString::Printf(TEXT("%s=%s"), *Pin->PinName.ToString(), *Input(*Pin, Depth + 1)));
			}
			return FString::Join(Arguments, TEXT(", "));
		}

		/** Target.Call(arguments); the target is written when the self pin is wired to another object. */
		FString Call(const UEdGraphNode& Node, int32 Depth)
		{
			const UEdGraphPin* Self = Node.FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input);
			const FString Target = Self && !Self->LinkedTo.IsEmpty() ? Input(*Self, Depth + 1) + TEXT(".") : FString();
			return FString::Printf(TEXT("%s%s(%s)"), *Target, *GetCallName(Node), *Arguments(Node, Depth));
		}

		/** The expression for the value of an output pin. */
		FString Source(const UEdGraphPin& Output, int32 Depth)
		{
			const UEdGraphNode* Node = Output.GetOwningNodeUnchecked();
			if (!Node || Depth > MaxExpressionDepth)
			{
				return TEXT("...");
			}
			const FString PinName = Output.PinName.ToString();
			if (Node->IsA<UK2Node_Knot>())
			{
				const UEdGraphPin* const* KnotInput = Node->Pins.FindByPredicate([](const UEdGraphPin* Pin) { return Pin && Pin->Direction == EGPD_Input; });
				return KnotInput ? Input(**KnotInput, Depth) : FString(TEXT("none"));
			}
			if (const FString* Result = Results.Find(Node))
			{
				return GetDataPins(*Node, EGPD_Output).Num() > 1 ? *Result + TEXT(".") + PinName : *Result;
			}
			if (Node->IsA<UK2Node_FunctionEntry>() || Node->IsA<UK2Node_Event>() || Node->IsA<UK2Node_Tunnel>())
			{
				return PinName;
			}
			if (const UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node))
			{
				const UEdGraphPin* Self = Node->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input);
				const FString Target = Self && !Self->bHidden && !Self->LinkedTo.IsEmpty() ? Input(*Self, Depth + 1) + TEXT(".") : FString();
				return Target + Variable->GetVarNameString();
			}
			if (HasExecPins(*Node))
			{
				// An output of a node that runs on another path.
				return FString::Printf(TEXT("%s.%s"), *Node->GetName(), *PinName);
			}

			const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
			const UFunction* Function = CallNode ? CallNode->GetTargetFunction() : nullptr;
			const FString FunctionName = Function ? Function->GetName() : FString();
			if (const TCHAR* Operator = GetOperator(FunctionName))
			{
				TArray<FString> Operands;
				for (const UEdGraphPin* Pin : GetDataPins(*Node, EGPD_Input))
				{
					Operands.Add(Input(*Pin, Depth + 1));
				}
				return TEXT("(") + FString::Join(Operands, *FString::Printf(TEXT(" %s "), Operator)) + TEXT(")");
			}
			if (FunctionName == TEXT("Not_PreBool"))
			{
				const TArray<const UEdGraphPin*> Inputs = GetDataPins(*Node, EGPD_Input);
				return Inputs.IsEmpty() ? FString(TEXT("!none")) : TEXT("!") + Input(*Inputs[0], Depth + 1);
			}
			const FString Expression = Call(*Node, Depth);
			return GetDataPins(*Node, EGPD_Output).Num() > 1 ? Expression + TEXT(".") + PinName : Expression;
		}

		void WriteEntry(const UEdGraphNode& Entry)
		{
			TArray<FString> Parameters;
			for (const UEdGraphPin* Pin : GetDataPins(Entry, EGPD_Output))
			{
				if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Delegate)
				{
					Parameters.Add(FString::Printf(TEXT("%s: %s"), *Pin->PinName.ToString(), *GetPinTypeText(Pin->PinType)));
				}
			}
			const UEdGraphPin* const* Then = Entry.Pins.FindByPredicate([](const UEdGraphPin* Pin) { return IsExecPin(Pin) && Pin->Direction == EGPD_Output; });
			const bool bLinked = Then && !(*Then)->LinkedTo.IsEmpty();
			// The disabled event nodes of a new Blueprint say nothing about it.
			if (!bLinked && Entry.IsAutomaticallyPlacedGhostNode())
			{
				return;
			}

			FString Title = GetNodeTitle(Entry);
			Title.RemoveFromStart(TEXT("Event "));
			const FString Kind = Entry.IsA<UK2Node_FunctionEntry>() ? TEXT("function") : (Entry.IsA<UK2Node_Event>() ? TEXT("event") : TEXT("entry"));
			AddLine(0, FString::Printf(TEXT("%s %s(%s):%s"), *Kind, *Title.Replace(TEXT(" "), TEXT("")), *FString::Join(Parameters, TEXT(", ")), *NodeSuffix(Entry)));
			if (bLinked)
			{
				WriteChain(*Then, 1);
			}
			else
			{
				AddLine(1, TEXT("(empty)"));
			}
			AddLine(0, FString());
		}

		FString NodeSuffix(const UEdGraphNode& Node) const
		{
			FString Suffix;
			if (Node.GetDesiredEnabledState() == ENodeEnabledState::Disabled)
			{
				Suffix += TEXT("  # disabled, not compiled");
			}
			if (bNodeNames)
			{
				Suffix += TEXT("  # ") + Node.GetName();
			}
			return Suffix;
		}

		/** Follows the execution wire of an output pin, one statement per node, until the path ends or meets a node already written. */
		void WriteChain(const UEdGraphPin* ExecOutput, int32 Indent)
		{
			while (ExecOutput && !ExecOutput->LinkedTo.IsEmpty() && ExecOutput->LinkedTo[0])
			{
				const UEdGraphNode* Node = ExecOutput->LinkedTo[0]->GetOwningNodeUnchecked();
				if (!Node)
				{
					return;
				}
				if (Node->IsA<UK2Node_Knot>())
				{
					const UEdGraphPin* const* KnotOutput = Node->Pins.FindByPredicate([](const UEdGraphPin* Pin) { return Pin && Pin->Direction == EGPD_Output; });
					ExecOutput = KnotOutput ? *KnotOutput : nullptr;
					continue;
				}
				if (Emitted.Contains(Node))
				{
					AddLine(Indent, TEXT("goto ") + Node->GetName());
					return;
				}
				Emitted.Add(Node);
				if (Labeled.Contains(Node))
				{
					AddLine(Indent, Node->GetName() + TEXT(":"));
				}
				if (!Node->NodeComment.IsEmpty())
				{
					AddLine(Indent, TEXT("# ") + Node->NodeComment.Replace(TEXT("\n"), TEXT(" ")));
				}

				TArray<const UEdGraphPin*> ExecOutputs;
				for (const UEdGraphPin* Pin : Node->Pins)
				{
					if (IsExecPin(Pin) && Pin->Direction == EGPD_Output && !Pin->LinkedTo.IsEmpty())
					{
						ExecOutputs.Add(Pin);
					}
				}

				if (Node->IsA<UK2Node_IfThenElse>())
				{
					const UEdGraphPin* Condition = Node->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input);
					AddLine(Indent, TEXT("if ") + (Condition ? Input(*Condition, 0) : FString(TEXT("none"))) + TEXT(":") + NodeSuffix(*Node));
					const UEdGraphPin* ThenPin = Node->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
					const UEdGraphPin* ElsePin = Node->FindPin(UEdGraphSchema_K2::PN_Else, EGPD_Output);
					WriteBlock(ThenPin, Indent + 1);
					if (ElsePin && !ElsePin->LinkedTo.IsEmpty())
					{
						AddLine(Indent, TEXT("else:"));
						WriteBlock(ElsePin, Indent + 1);
					}
					return;
				}

				WriteStatement(*Node, Indent);
				if (ExecOutputs.Num() == 1 && ExecOutputs[0]->PinName == UEdGraphSchema_K2::PN_Then)
				{
					ExecOutput = ExecOutputs[0];
					continue;
				}
				for (const UEdGraphPin* Pin : ExecOutputs)
				{
					AddLine(Indent + 1, FString::Printf(TEXT("on %s:"), *Pin->PinName.ToString()));
					WriteBlock(Pin, Indent + 2);
				}
				return;
			}
		}

		void WriteBlock(const UEdGraphPin* ExecOutput, int32 Indent)
		{
			const int32 LinesBefore = Lines.Num();
			WriteChain(ExecOutput, Indent);
			if (Lines.Num() == LinesBefore)
			{
				AddLine(Indent, TEXT("(nothing)"));
			}
		}

		void WriteStatement(const UEdGraphNode& Node, int32 Indent)
		{
			if (const UK2Node_VariableSet* Set = Cast<UK2Node_VariableSet>(&Node))
			{
				const UEdGraphPin* Value = Node.FindPin(Set->GetVarName(), EGPD_Input);
				AddLine(Indent, FString::Printf(TEXT("%s = %s%s"), *Set->GetVarNameString(), Value ? *Input(*Value, 0) : TEXT("none"), *NodeSuffix(Node)));
				Results.Add(&Node, Set->GetVarNameString());
				return;
			}
			if (Node.IsA<UK2Node_FunctionResult>())
			{
				AddLine(Indent, FString::Printf(TEXT("return %s%s"), *Arguments(Node, 0), *NodeSuffix(Node)));
				return;
			}

			const FString Statement = Call(Node, 0);
			const bool bUsedOutput = GetDataPins(Node, EGPD_Output).ContainsByPredicate([](const UEdGraphPin* Pin) { return !Pin->LinkedTo.IsEmpty(); });
			if (bUsedOutput)
			{
				const FString Name = FString::Printf(TEXT("$%d"), ++ResultCount);
				Results.Add(&Node, Name);
				AddLine(Indent, FString::Printf(TEXT("%s = %s%s"), *Name, *Statement, *NodeSuffix(Node)));
			}
			else
			{
				AddLine(Indent, Statement + NodeSuffix(Node));
			}
		}
	};
}

FAgentMcpGraphText UAgentMcpBlueprintTools::ReadGraph(UBlueprint* Blueprint, const FString& Graph, bool bNodeNames, int32 MaxChars)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::BlueprintGraphToolsPrivate;

	FAgentMcpGraphText Result;
	if (!Tools::RequireObject(Blueprint, TEXT("blueprint")))
	{
		return Result;
	}
	TArray<UEdGraph*> Graphs;
	if (Graph.IsEmpty())
	{
		Graphs.Append(Blueprint->UbergraphPages);
		Graphs.Append(Blueprint->FunctionGraphs);
		Graphs.Append(Blueprint->MacroGraphs);
	}
	else if (UEdGraph* EdGraph = FindGraph(Blueprint, Graph))
	{
		Graphs.Add(EdGraph);
	}
	else
	{
		return Result;
	}

	Result.Blueprint = Blueprint->GetPathName();
	TArray<FString> Sections;
	for (const UEdGraph* EdGraph : Graphs)
	{
		if (EdGraph)
		{
			Sections.Add(FString::Printf(TEXT("== %s\n%s"), *EdGraph->GetName(), *FGraphWriter(*EdGraph, bNodeNames).Write()));
		}
	}
	Result.Text = FString::Join(Sections, TEXT("\n"));
	// Results above MaxResultBytes (64 KB) are cut; escaping and multi-byte text make the JSON larger than the text.
	const int32 Limit = FMath::Clamp(MaxChars, 1000, 50000);
	if (Result.Text.Len() > Limit)
	{
		Result.Text.LeftInline(Limit);
		Result.bTruncated = true;
	}
	return Result;
}
