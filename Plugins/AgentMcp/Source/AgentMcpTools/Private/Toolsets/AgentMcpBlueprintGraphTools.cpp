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
#include "K2Node_AddPinInterface.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Switch.h"
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

	Found.Sort([](const FAgentMcpNodeType& A, const FAgentMcpNodeType& B) { return A.TypeId < B.TypeId; });
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
						*INameValidatorInterface::GetErrorText(EventName, Validity == EValidatorResult::Ok ? EValidatorResult::AlreadyInUse : Validity).BuildSourceString()));
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
