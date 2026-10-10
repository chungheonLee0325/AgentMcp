#include "AgentMcpBlueprintTools.h"

#include "AgentMcpAsyncResult.h"
#include "AgentMcpToolsCommon.h"

#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "FindInBlueprintManager.h"
#include "Misc/PackageName.h"

// blueprint_find_usages runs the search of the editor's Find in Blueprints tab (SFindInBlueprints::MakeSearchQuery): an FStreamSearch
// thread over the index that Blueprints keep in their asset registry tags, so Blueprints that are not loaded are searched too.
namespace UE::AgentMcp::BlueprintSearchToolsPrivate
{
	constexpr int32 MaxUsages = 500;

	/** Stops the search thread when the tool result goes away, also when the call was cancelled or timed out. */
	struct FSearchOwner
	{
		TSharedRef<FStreamSearch> Search;

		explicit FSearchOwner(const FString& Query)
			: Search(MakeShared<FStreamSearch>(Query))
		{
		}

		~FSearchOwner()
		{
			Search->EnsureCompletion();
		}
	};

	/** Category of a result in source text: Graph, Function, Macro, Event, Function Call, Variable, Pin and so on. */
	FString GetKind(const FFindInBlueprintsResult& Result)
	{
		return Result.GetCategory().BuildSourceString();
	}

	/** Adds a node, variable or component and the pins under it that matched. FinalizeSearchData resolves node classes, so it runs first. */
	void AddUsage(const TSharedPtr<FFindInBlueprintsResult>& Item, UBlueprint* Blueprint, const FString& BlueprintPath, const FString& Graph, int32 Limit,
		FAgentMcpBlueprintUsageResult& Out)
	{
		if (Out.Usages.Num() >= Limit)
		{
			Out.bTruncated = true;
			return;
		}
		Item->FinalizeSearchData();
		FAgentMcpBlueprintUsage& Usage = Out.Usages.AddDefaulted_GetRef();
		Usage.Blueprint = BlueprintPath;
		Usage.Graph = Graph;
		Usage.Kind = GetKind(*Item);
		Usage.Title = Item->DisplayText.ToString();
		Usage.Comment = Item->GetCommentText();
		// The index holds display names, which the editor language changes (EventGraph is 이벤트그래프 in Korean); the node gives the names
		// that blueprint_get_graph takes.
		if (Blueprint && !Graph.IsEmpty())
		{
			if (const UEdGraphNode* Node = Cast<UEdGraphNode>(Item->GetObject(Blueprint)))
			{
				Usage.Node = Node->GetName();
				Usage.Kind = Node->GetClass()->GetName();
				if (const UEdGraph* NodeGraph = Node->GetGraph())
				{
					Usage.Graph = NodeGraph->GetName();
				}
			}
		}
		for (const TSharedPtr<FFindInBlueprintsResult>& Child : Item->Children)
		{
			if (Child.IsValid())
			{
				Child->FinalizeSearchData();
				Usage.Matches.Add(FString::Printf(TEXT("%s: %s"), *GetKind(*Child), *Child->DisplayText.ToString()));
			}
		}
	}
}

UAgentMcpAsyncResult* UAgentMcpBlueprintTools::FindUsages(const FString& Query, bool bIncludeEngineContent, int32 MaxResults, float TimeoutSeconds)
{
	using namespace UE::AgentMcp::BlueprintSearchToolsPrivate;

	if (Query.TrimStartAndEnd().IsEmpty())
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("'query' is empty."), TEXT("Pass words to find, such as a function, variable or Blueprint name."));
		return nullptr;
	}

	const int32 Limit = FMath::Clamp(MaxResults, 1, MaxUsages);
	const TSharedRef<FSearchOwner> Owner = MakeShared<FSearchOwner>(Query);
	return UAgentMcpAsyncResult::Create(FMath::Clamp(TimeoutSeconds, 5.0f, 600.0f), [Owner, Limit, bIncludeEngineContent](UAgentMcpAsyncResult& Result)
	{
		if (!Owner->Search->IsComplete())
		{
			return;
		}

		TArray<TSharedPtr<FFindInBlueprintsResult>> Roots;
		Owner->Search->GetFilteredItems(Roots);

		FAgentMcpBlueprintUsageResult Out;
		Out.OutOfDateBlueprints = Owner->Search->GetOutOfDateCount();
		Out.UnindexedBlueprints = FFindInBlueprintSearchManager::Get().GetNumberUnindexedAssets();
		for (const TSharedPtr<FFindInBlueprintsResult>& Root : Roots)
		{
			if (!Root.IsValid())
			{
				continue;
			}
			// The root is the Blueprint, or the level of a level Blueprint; loading it lets nodes be named.
			const FString BlueprintPath = Root->DisplayText.ToString();
			if (!bIncludeEngineContent && !UE::AgentMcp::Tools::IsProjectContentPackage(FPackageName::ObjectPathToPackageName(BlueprintPath)))
			{
				continue;
			}
			++Out.BlueprintCount;
			UBlueprint* Blueprint = Root->GetParentBlueprint();
			for (const TSharedPtr<FFindInBlueprintsResult>& Child : Root->Children)
			{
				if (!Child.IsValid())
				{
					continue;
				}
				const FString Kind = GetKind(*Child);
				if (Kind == TEXT("Graph") || Kind == TEXT("Function") || Kind == TEXT("Macro"))
				{
					for (const TSharedPtr<FFindInBlueprintsResult>& Node : Child->Children)
					{
						if (Node.IsValid())
						{
							AddUsage(Node, Blueprint, BlueprintPath, Child->DisplayText.ToString(), Limit, Out);
						}
					}
				}
				else
				{
					AddUsage(Child, Blueprint, BlueprintPath, FString(), Limit, Out);
				}
			}
		}
		Result.CompleteWith(Out);
	});
}
