#include "AgentMcpActorTools.h"

#include "AgentMcpToolsCommon.h"

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"

namespace UE::AgentMcp::ActorToolsPrivate
{
	constexpr int32 MaxFindLimit = 500;
	constexpr int32 MaxListedComponents = 500;
	constexpr int32 MaxListedAttachedActors = 100;

	FString GetFolder(const AActor* Actor)
	{
		const FName FolderPath = Actor->GetFolderPath();
		return FolderPath.IsNone() ? FString() : FolderPath.ToString();
	}

	FAgentMcpActorSummary MakeSummary(const AActor* Actor)
	{
		FAgentMcpActorSummary Summary;
		Summary.Label = Actor->GetActorLabel();
		Summary.Name = Actor->GetName();
		Summary.Path = Actor->GetPathName();
		Summary.ClassName = Actor->GetClass()->GetName();
		Summary.Folder = GetFolder(Actor);
		return Summary;
	}

	bool MatchesName(const AActor* Actor, const FString& Pattern, bool bWildcard)
	{
		const FString& Label = Actor->GetActorLabel();
		const FString ObjectName = Actor->GetName();
		return bWildcard
			? (Label.MatchesWildcard(Pattern) || ObjectName.MatchesWildcard(Pattern))
			: (Label.Contains(Pattern) || ObjectName.Contains(Pattern));
	}

	bool IsInFolder(const AActor* Actor, const FString& Folder)
	{
		const FString ActorFolder = GetFolder(Actor);
		return ActorFolder.Equals(Folder, ESearchCase::IgnoreCase) || ActorFolder.StartsWith(Folder + TEXT("/"), ESearchCase::IgnoreCase);
	}

	/** Editor tools change the editor level only; play session actors are discarded when the session ends. */
	bool RequireEditorLevelActor(const AActor* Actor)
	{
		const UWorld* World = Actor->GetWorld();
		if (World && !World->IsGameWorld())
		{
			return true;
		}
		RaiseToolError(TEXT("NOT_SUPPORTED"), FString::Printf(TEXT("%s is not in the editor level."), *Actor->GetPathName()),
			TEXT("Stop the play session and address the actor in the editor level (actor_find with world Editor)."));
		return false;
	}
}

FAgentMcpActorFindResult UAgentMcpActorTools::Find(const FString& Name, TSubclassOf<AActor> ActorClass, const FString& Tag, const FString& Folder, bool bSelectedOnly, EAgentMcpWorld World, int32 Limit, int32 Cursor)
{
	using namespace UE::AgentMcp::ActorToolsPrivate;

	FAgentMcpActorFindResult Result;
	UWorld* TargetWorld = UE::AgentMcp::Tools::ResolveWorld(World);
	if (!TargetWorld)
	{
		return Result;
	}
	Result.World = UE::AgentMcp::Tools::GetWorldKind(TargetWorld);
	Result.WorldPackage = TargetWorld->GetPackage()->GetName();

	const FString NamePattern = Name.TrimStartAndEnd();
	const bool bWildcard = NamePattern.Contains(TEXT("*")) || NamePattern.Contains(TEXT("?"));

	const FString TagText = Tag.TrimStartAndEnd();
	if (TagText.Len() >= NAME_SIZE)
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("'tag' is too long."));
		return Result;
	}
	const FName TagName = TagText.IsEmpty() ? NAME_None : FName(*TagText);

	FString FolderFilter = Folder.TrimStartAndEnd();
	FolderFilter.RemoveFromEnd(TEXT("/"));

	TArray<AActor*> Matches;
	for (TActorIterator<AActor> It(TargetWorld); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor) || !Actor->IsListedInSceneOutliner())
		{
			continue;
		}
		if ((ActorClass && !Actor->IsA(ActorClass))
			|| (!NamePattern.IsEmpty() && !MatchesName(Actor, NamePattern, bWildcard))
			|| (!TagName.IsNone() && !Actor->ActorHasTag(TagName))
			|| (!FolderFilter.IsEmpty() && !IsInFolder(Actor, FolderFilter))
			|| (bSelectedOnly && !Actor->IsSelectedInEditor()))
		{
			continue;
		}
		Matches.Add(Actor);
	}

	// Stable order so cursors address the same actors while the level is unchanged.
	Matches.Sort([](const AActor& A, const AActor& B)
	{
		const int32 Order = A.GetActorLabel().Compare(B.GetActorLabel(), ESearchCase::IgnoreCase);
		return Order != 0 ? Order < 0 : A.GetPathName() < B.GetPathName();
	});

	int32 Start = 0;
	int32 End = 0;
	int32 NextCursor = -1;
	if (!UE::AgentMcp::Tools::GetPage(Matches.Num(), Limit, Cursor, MaxFindLimit, Start, End, NextCursor))
	{
		return Result;
	}

	Result.TotalMatched = Matches.Num();
	Result.NextCursor = NextCursor;
	for (int32 Index = Start; Index < End; ++Index)
	{
		Result.Actors.Add(MakeSummary(Matches[Index]));
	}
	return Result;
}

FAgentMcpActorDetails UAgentMcpActorTools::Inspect(AActor* Actor, bool bIncludeComponents, int32 MaxComponents)
{
	using namespace UE::AgentMcp::ActorToolsPrivate;

	FAgentMcpActorDetails Details;
	if (!UE::AgentMcp::Tools::RequireObject(Actor, TEXT("actor")))
	{
		return Details;
	}

	Details.Label = Actor->GetActorLabel();
	Details.Name = Actor->GetName();
	Details.Path = Actor->GetPathName();
	Details.ClassPath = Actor->GetClass()->GetPathName();
	if (const UBlueprint* Blueprint = Cast<UBlueprint>(Actor->GetClass()->ClassGeneratedBy))
	{
		Details.Blueprint = Blueprint->GetPathName();
	}
	Details.Folder = GetFolder(Actor);
	Details.World = UE::AgentMcp::Tools::GetWorldKind(Actor->GetWorld());
	if (const ULevel* Level = Actor->GetLevel())
	{
		Details.Level = Level->GetPackage()->GetName();
	}
	if (Actor->IsPackageExternal())
	{
		Details.ExternalPackage = Actor->GetPackage()->GetName();
	}
	for (const FName& ActorTag : Actor->Tags)
	{
		Details.Tags.Add(ActorTag.ToString());
	}
	Details.bHiddenInGame = Actor->IsHidden();
	Details.bHiddenInEditor = Actor->IsHiddenEd();
	Details.Transform = UE::AgentMcp::Tools::MakeTransformValue(Actor->GetActorTransform());

	if (const AActor* ParentActor = Actor->GetAttachParentActor())
	{
		Details.AttachParent = ParentActor->GetPathName();
	}
	TArray<AActor*> AttachedActors;
	Actor->GetAttachedActors(AttachedActors);
	for (const AActor* AttachedActor : AttachedActors)
	{
		if (Details.AttachedActors.Num() >= MaxListedAttachedActors)
		{
			break;
		}
		if (AttachedActor)
		{
			Details.AttachedActors.Add(AttachedActor->GetPathName());
		}
	}

	const TInlineComponentArray<UActorComponent*> Components(Actor);
	Details.ComponentCount = Components.Num();
	if (bIncludeComponents)
	{
		const int32 ComponentLimit = FMath::Clamp(MaxComponents, 0, MaxListedComponents);
		const USceneComponent* RootComponent = Actor->GetRootComponent();
		for (const UActorComponent* Component : Components)
		{
			if (Details.Components.Num() >= ComponentLimit)
			{
				break;
			}
			if (!IsValid(Component))
			{
				continue;
			}

			FAgentMcpComponentSummary& Summary = Details.Components.AddDefaulted_GetRef();
			Summary.Name = Component->GetName();
			Summary.ClassName = Component->GetClass()->GetName();
			Summary.Path = Component->GetPathName();
			Summary.bIsRoot = Component == RootComponent;
			if (const USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
			{
				if (const USceneComponent* ParentComponent = SceneComponent->GetAttachParent())
				{
					Summary.AttachParent = ParentComponent->GetName();
				}
			}
		}
	}
	return Details;
}

FAgentMcpSetTransformResult UAgentMcpActorTools::SetTransform(AActor* Actor, const TArray<double>& Location, const TArray<double>& Rotation, const TArray<double>& Scale)
{
	using namespace UE::AgentMcp::ActorToolsPrivate;

	FAgentMcpSetTransformResult Result;
	if (!UE::AgentMcp::Tools::RequireObject(Actor, TEXT("actor")) || !RequireEditorLevelActor(Actor))
	{
		return Result;
	}

	bool bHasLocation = false;
	bool bHasRotation = false;
	bool bHasScale = false;
	FVector NewLocation = FVector::ZeroVector;
	FVector NewRotation = FVector::ZeroVector;
	FVector NewScale = FVector::OneVector;
	if (!UE::AgentMcp::Tools::ReadOptionalVector(Location, TEXT("location"), bHasLocation, NewLocation)
		|| !UE::AgentMcp::Tools::ReadOptionalVector(Rotation, TEXT("rotation"), bHasRotation, NewRotation)
		|| !UE::AgentMcp::Tools::ReadOptionalVector(Scale, TEXT("scale"), bHasScale, NewScale))
	{
		return Result;
	}
	if (!bHasLocation && !bHasRotation && !bHasScale)
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("Pass at least one of location, rotation or scale."));
		return Result;
	}
	if (!Actor->GetRootComponent())
	{
		UE::AgentMcp::RaiseToolError(TEXT("NOT_SUPPORTED"), FString::Printf(TEXT("%s has no root component to transform."), *Actor->GetPathName()));
		return Result;
	}
	if (!Actor->CanModify())
	{
		UE::AgentMcp::RaiseToolError(TEXT("NOT_EDITABLE"), FString::Printf(TEXT("%s cannot be modified, for example because its level is locked."), *Actor->GetPathName()));
		return Result;
	}

	const FTransform Before = Actor->GetActorTransform();
	FTransform After = Before;
	if (bHasLocation)
	{
		After.SetLocation(NewLocation);
	}
	if (bHasRotation)
	{
		After.SetRotation(FRotator(NewRotation.X, NewRotation.Y, NewRotation.Z).Quaternion());
	}
	if (bHasScale)
	{
		After.SetScale3D(NewScale);
	}

	Result.Actor = Actor->GetPathName();
	Result.Before = UE::AgentMcp::Tools::MakeTransformValue(Before);

	// Record the actor and its root component for undo, move it, then let it react as after an editor move
	// (PostEditMove reruns construction scripts and updates dependent state).
	Actor->Modify();
	Actor->SetActorTransform(After, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	Actor->PostEditMove(/*bFinished=*/true);
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}

	Result.After = UE::AgentMcp::Tools::MakeTransformValue(Actor->GetActorTransform());
	return Result;
}
