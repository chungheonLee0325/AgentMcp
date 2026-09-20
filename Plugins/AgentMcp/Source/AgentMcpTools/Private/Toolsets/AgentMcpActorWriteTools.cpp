#include "AgentMcpActorTools.h"

#include "AgentMcpToolsCommon.h"

#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UObject/Package.h"
#include "UObject/PropertyAccessUtil.h"

namespace UE::AgentMcp::ActorWriteToolsPrivate
{
	constexpr int32 MaxSpawnEntries = 200;
	constexpr int32 MaxDuplicates = 100;

	struct FProblems
	{
		TArray<FString> Messages;
		TSet<FString> Codes;

		void Add(const FString& Message, const TCHAR* Code)
		{
			Messages.Add(Message);
			Codes.Add(Code);
		}

		bool IsEmpty() const
		{
			return Messages.IsEmpty();
		}
	};

	UWorld* RequireEditorWorld()
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			RaiseToolError(TEXT("EDITOR_UNAVAILABLE"), TEXT("No level is open in the editor."));
		}
		return World;
	}

	UEditorActorSubsystem* RequireActorSubsystem()
	{
		UEditorActorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
		if (!Subsystem)
		{
			RaiseToolError(TEXT("EDITOR_UNAVAILABLE"), TEXT("The editor actor subsystem is not available."));
		}
		return Subsystem;
	}

	bool IsEditorLevelActor(const AActor* Actor)
	{
		const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
		return World && !World->IsGameWorld();
	}

	/** An actor of the editor level by object path, label or object name. Adds the problem and returns null when it does not resolve. */
	AActor* FindEditorActor(UWorld* World, const FString& Text, const FString& Label, FProblems& Problems)
	{
		const FString Trimmed = Text.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			Problems.Add(FString::Printf(TEXT("%s is empty"), *Label), TEXT("INVALID_ARGUMENT"));
			return nullptr;
		}
		if (Trimmed.StartsWith(TEXT("/")))
		{
			AActor* Actor = Cast<AActor>(StaticFindObject(AActor::StaticClass(), nullptr, *Trimmed));
			if (!IsValid(Actor) || !IsEditorLevelActor(Actor))
			{
				Problems.Add(FString::Printf(TEXT("%s: no actor of the editor level at %s"), *Label, *Trimmed.Left(200)), TEXT("NOT_FOUND"));
				return nullptr;
			}
			return Actor;
		}

		TArray<AActor*> Matches;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (IsValid(Actor) && (Actor->GetActorLabel().Equals(Trimmed, ESearchCase::IgnoreCase) || Actor->GetName().Equals(Trimmed, ESearchCase::IgnoreCase)))
			{
				Matches.Add(Actor);
			}
		}
		if (Matches.IsEmpty())
		{
			Problems.Add(FString::Printf(TEXT("%s: no actor is called %s"), *Label, *Trimmed.Left(200)), TEXT("NOT_FOUND"));
			return nullptr;
		}
		if (Matches.Num() > 1)
		{
			TArray<FString> Paths;
			for (const AActor* Match : Matches)
			{
				Paths.Add(Match->GetPathName());
			}
			Problems.Add(FString::Printf(TEXT("%s: %s names several actors (%s)"), *Label, *Trimmed.Left(64), *FString::Join(Paths, TEXT(", "))), TEXT("AMBIGUOUS_REFERENCE"));
			return nullptr;
		}
		return Matches[0];
	}

	/** Reads an optional [X, Y, Z] field of a JSON entry. */
	bool ReadVectorField(const FJsonObject& Entry, const TCHAR* Field, const FString& Label, FProblems& Problems, bool& bOutProvided, FVector& OutVector)
	{
		bOutProvided = false;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Entry.TryGetArrayField(Field, Values) || !Values)
		{
			return true;
		}
		if (Values->Num() != 3)
		{
			Problems.Add(FString::Printf(TEXT("%s: '%s' needs three numbers"), *Label, Field), TEXT("INVALID_ARGUMENT"));
			return false;
		}
		double Numbers[3] = { 0.0, 0.0, 0.0 };
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (!(*Values)[Index]->TryGetNumber(Numbers[Index]) || !FMath::IsFinite(Numbers[Index]))
			{
				Problems.Add(FString::Printf(TEXT("%s: '%s' must hold finite numbers"), *Label, Field), TEXT("INVALID_ARGUMENT"));
				return false;
			}
		}
		bOutProvided = true;
		OutVector = FVector(Numbers[0], Numbers[1], Numbers[2]);
		return true;
	}

	struct FTransformFields
	{
		bool bHasLocation = false;
		bool bHasRotation = false;
		bool bHasScale = false;
		FVector Location = FVector::ZeroVector;
		FVector Rotation = FVector::ZeroVector;
		FVector Scale = FVector::OneVector;

		FTransform Apply(const FTransform& Base) const
		{
			FTransform Result = Base;
			if (bHasLocation)
			{
				Result.SetLocation(Location);
			}
			if (bHasRotation)
			{
				Result.SetRotation(FRotator(Rotation.X, Rotation.Y, Rotation.Z).Quaternion());
			}
			if (bHasScale)
			{
				Result.SetScale3D(Scale);
			}
			return Result;
		}
	};

	bool ReadTransformFields(const FJsonObject& Entry, const FString& Label, FProblems& Problems, FTransformFields& OutFields)
	{
		return ReadVectorField(Entry, TEXT("location"), Label, Problems, OutFields.bHasLocation, OutFields.Location)
			& ReadVectorField(Entry, TEXT("rotation"), Label, Problems, OutFields.bHasRotation, OutFields.Rotation)
			& ReadVectorField(Entry, TEXT("scale"), Label, Problems, OutFields.bHasScale, OutFields.Scale);
	}

	/** Copies prepared values onto a freshly spawned actor; the dispatcher transaction rolls back a failure. */
	bool ApplyProperties(AActor* Actor, const FJsonObject& Values, const FString& Label)
	{
		Tools::FPreparedPropertyValues Prepared;
		TArray<FString> Problems;
		TSet<FString> Codes;
		if (!Prepared.Prepare(Actor, Values, Problems, Codes))
		{
			Tools::RaiseProblems(FString::Printf(TEXT("%s: the properties could not be set on %s"), *Label, *Actor->GetActorLabel()), Problems, Codes,
				TEXT("object_list_properties shows property names, types and whether each property is editable."));
			return false;
		}
		for (const Tools::FPreparedPropertyValues::FEntry& Entry : Prepared.GetEntries())
		{
			Actor->Modify();
			const EPropertyAccessResultFlags SetResult = PropertyAccessUtil::SetPropertyValue_Object(
				Entry.Property, Actor, Entry.Property, Entry.Value, INDEX_NONE, PropertyAccessUtil::EditorReadOnlyFlags, EPropertyAccessChangeNotifyMode::Default);
			if (SetResult != EPropertyAccessResultFlags::Success)
			{
				RaiseToolError(TEXT("PROPERTY_WRITE_FAILED"), FString::Printf(TEXT("%s: setting '%s' on %s failed."), *Label, *Entry.Property->GetName(), *Actor->GetPathName()));
				return false;
			}
		}
		return true;
	}

	FAgentMcpActorSummary MakeSummary(const AActor* Actor)
	{
		FAgentMcpActorSummary Summary;
		Summary.Label = Actor->GetActorLabel();
		Summary.Name = Actor->GetName();
		Summary.Path = Actor->GetPathName();
		Summary.ClassName = Actor->GetClass()->GetName();
		const FName Folder = Actor->GetFolderPath();
		Summary.Folder = Folder.IsNone() ? FString() : Folder.ToString();
		return Summary;
	}

	/** One planned spawn, fully checked. */
	struct FPlannedSpawn
	{
		UObject* Asset = nullptr;
		UClass* Class = nullptr;
		FString ActorLabel;
		FString Folder;
		FTransformFields Transform;
		TSharedPtr<FJsonObject> Properties;
	};

	void CollectAttached(const AActor* Actor, TSet<const AActor*>& Visited, TArray<AActor*>& OutActors)
	{
		TArray<AActor*> Attached;
		Actor->GetAttachedActors(Attached);
		for (AActor* Child : Attached)
		{
			if (IsValid(Child) && !Visited.Contains(Child))
			{
				Visited.Add(Child);
				OutActors.Add(Child);
				CollectAttached(Child, Visited, OutActors);
			}
		}
	}
}

FAgentMcpActorBatchResult UAgentMcpActorTools::Spawn(const TArray<FJsonObjectWrapper>& Actors)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::ActorWriteToolsPrivate;

	FAgentMcpActorBatchResult Result;
	UWorld* World = RequireEditorWorld();
	UEditorActorSubsystem* Subsystem = World ? RequireActorSubsystem() : nullptr;
	if (!Subsystem)
	{
		return Result;
	}
	const FString EntryHint = TEXT("Each entry is {\"asset\": mesh or Blueprint path, or \"class\": class name, \"location\": [X, Y, Z]}, for example {\"asset\": \"/Engine/BasicShapes/Cube\", \"location\": [0, 0, 100]}.");
	if (Actors.IsEmpty() || Actors.Num() > MaxSpawnEntries)
	{
		RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("'actors' must list 1-%d entries, not %d."), MaxSpawnEntries, Actors.Num()), EntryHint);
		return Result;
	}

	// Check every entry first, so a typo in the last one does not leave half a row of actors behind.
	static const TSet<FString> KnownFields = { TEXT("asset"), TEXT("class"), TEXT("label"), TEXT("location"), TEXT("rotation"), TEXT("scale"), TEXT("folder"), TEXT("properties") };
	TArray<FPlannedSpawn> Planned;
	FProblems Problems;
	for (int32 Index = 0; Index < Actors.Num(); ++Index)
	{
		const FString Label = FString::Printf(TEXT("actors[%d]"), Index);
		const TSharedPtr<FJsonObject>& Entry = Actors[Index].JsonObject;
		if (!Entry.IsValid())
		{
			Problems.Add(FString::Printf(TEXT("%s is not an object"), *Label), TEXT("INVALID_ARGUMENT"));
			continue;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Entry->Values)
		{
			if (!KnownFields.Contains(Pair.Key))
			{
				Problems.Add(FString::Printf(TEXT("%s has the unknown field '%s'"), *Label, *Pair.Key.Left(64)), TEXT("INVALID_ARGUMENT"));
			}
		}

		FPlannedSpawn Plan;
		FString AssetPath;
		FString ClassName;
		Entry->TryGetStringField(TEXT("asset"), AssetPath);
		Entry->TryGetStringField(TEXT("class"), ClassName);
		AssetPath = AssetPath.TrimStartAndEnd();
		ClassName = ClassName.TrimStartAndEnd();
		if (AssetPath.IsEmpty() == ClassName.IsEmpty())
		{
			Problems.Add(FString::Printf(TEXT("%s needs either asset or class"), *Label), TEXT("INVALID_ARGUMENT"));
			continue;
		}
		if (!AssetPath.IsEmpty())
		{
			Plan.Asset = FSoftObjectPath(AssetPath.Contains(TEXT(".")) ? AssetPath : AssetPath + TEXT(".") + FPackageName::GetShortName(AssetPath)).TryLoad();
			if (!Plan.Asset)
			{
				Problems.Add(FString::Printf(TEXT("%s: no asset at %s"), *Label, *AssetPath.Left(200)), TEXT("NOT_FOUND"));
				continue;
			}
			if (const UBlueprint* Blueprint = Cast<UBlueprint>(Plan.Asset))
			{
				if (!Blueprint->GeneratedClass || !Blueprint->GeneratedClass->IsChildOf(AActor::StaticClass()))
				{
					Problems.Add(FString::Printf(TEXT("%s: %s is not an actor Blueprint"), *Label, *AssetPath.Left(200)), TEXT("NOT_SUPPORTED"));
					continue;
				}
			}
		}
		else
		{
			Plan.Class = ResolveClassName(ClassName);
			if (HasToolError())
			{
				return Result;
			}
			if (!Plan.Class)
			{
				Problems.Add(FString::Printf(TEXT("%s: no class called %s"), *Label, *ClassName.Left(200)), TEXT("NOT_FOUND"));
				continue;
			}
			if (!Plan.Class->IsChildOf(AActor::StaticClass()))
			{
				Problems.Add(FString::Printf(TEXT("%s: %s is not an actor class"), *Label, *Plan.Class->GetName()), TEXT("NOT_SUPPORTED"));
				continue;
			}
			if (Plan.Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				Problems.Add(FString::Printf(TEXT("%s: %s is abstract or deprecated"), *Label, *Plan.Class->GetName()), TEXT("INVALID_ARGUMENT"));
				continue;
			}
		}

		if (!ReadTransformFields(*Entry, Label, Problems, Plan.Transform))
		{
			continue;
		}
		Entry->TryGetStringField(TEXT("label"), Plan.ActorLabel);
		Entry->TryGetStringField(TEXT("folder"), Plan.Folder);
		Plan.ActorLabel = Plan.ActorLabel.TrimStartAndEnd();
		Plan.Folder = Plan.Folder.TrimStartAndEnd();

		const TSharedPtr<FJsonObject>* PropertyObject = nullptr;
		if (Entry->TryGetObjectField(TEXT("properties"), PropertyObject) && PropertyObject && PropertyObject->IsValid())
		{
			Plan.Properties = *PropertyObject;
			// The actor does not exist yet, so the values are checked against the class defaults as if they were an instance.
			const UClass* CheckClass = Plan.Class;
			if (!CheckClass)
			{
				const UBlueprint* Blueprint = Cast<UBlueprint>(Plan.Asset);
				CheckClass = Blueprint ? Blueprint->GeneratedClass.Get() : nullptr;
			}
			if (CheckClass)
			{
				Tools::FPreparedPropertyValues Check;
				TArray<FString> ValueProblems;
				TSet<FString> ValueCodes;
				if (!Check.Prepare(CheckClass->GetDefaultObject(), *Plan.Properties, ValueProblems, ValueCodes, /*bCheckAsInstance=*/true))
				{
					for (const FString& Problem : ValueProblems)
					{
						Problems.Add(FString::Printf(TEXT("%s: %s"), *Label, *Problem), ValueCodes.Num() == 1 ? **ValueCodes.CreateConstIterator() : TEXT("INVALID_ARGUMENT"));
					}
					continue;
				}
			}
		}
		Planned.Add(MoveTemp(Plan));
	}
	if (!Problems.IsEmpty())
	{
		Tools::RaiseProblems(TEXT("Nothing was spawned"), Problems.Messages, Problems.Codes, EntryHint);
		return Result;
	}

	for (int32 Index = 0; Index < Planned.Num(); ++Index)
	{
		const FPlannedSpawn& Plan = Planned[Index];
		const FVector Location = Plan.Transform.bHasLocation ? Plan.Transform.Location : FVector::ZeroVector;
		const FRotator Rotation = Plan.Transform.bHasRotation ? FRotator(Plan.Transform.Rotation.X, Plan.Transform.Rotation.Y, Plan.Transform.Rotation.Z) : FRotator::ZeroRotator;
		AActor* Actor = Plan.Asset
			? Subsystem->SpawnActorFromObject(Plan.Asset, Location, Rotation)
			: Subsystem->SpawnActorFromClass(Plan.Class, Location, Rotation);
		if (!IsValid(Actor))
		{
			RaiseToolError(TEXT("SPAWN_FAILED"), FString::Printf(TEXT("actors[%d] could not be spawned."), Index), TEXT("log_get_recent may show the reason."));
			return Result;
		}

		if (Plan.Transform.bHasScale && Actor->GetRootComponent())
		{
			Actor->SetActorScale3D(Plan.Transform.Scale);
		}
		if (!Plan.ActorLabel.IsEmpty())
		{
			Actor->SetActorLabel(Plan.ActorLabel);
		}
		if (!Plan.Folder.IsEmpty())
		{
			Actor->SetFolderPath(FName(*Plan.Folder));
		}
		if (Plan.Properties.IsValid() && !ApplyProperties(Actor, *Plan.Properties, FString::Printf(TEXT("actors[%d]"), Index)))
		{
			return Result;
		}
		Actor->PostEditMove(/*bFinished=*/true);
		Result.Actors.Add(MakeSummary(Actor));
	}

	Result.bApplied = true;
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
	return Result;
}

FAgentMcpActorBatchResult UAgentMcpActorTools::Duplicate(AActor* Actor, const TArray<double>& Offset, const TArray<FJsonObjectWrapper>& Transforms, int32 Count)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::ActorWriteToolsPrivate;

	FAgentMcpActorBatchResult Result;
	UWorld* World = RequireEditorWorld();
	UEditorActorSubsystem* Subsystem = World ? RequireActorSubsystem() : nullptr;
	if (!Subsystem || !Tools::RequireObject(Actor, TEXT("actor")))
	{
		return Result;
	}
	if (!IsEditorLevelActor(Actor))
	{
		RaiseToolError(TEXT("NOT_SUPPORTED"), FString::Printf(TEXT("%s is not in the editor level."), *Actor->GetPathName()),
			TEXT("Stop the play session and address the actor in the editor level (actor_find with world Editor)."));
		return Result;
	}

	bool bHasOffset = false;
	FVector OffsetStep = FVector::ZeroVector;
	if (!Tools::ReadOptionalVector(Offset, TEXT("offset"), bHasOffset, OffsetStep))
	{
		return Result;
	}

	FProblems Problems;
	TArray<FTransformFields> Placements;
	if (!Transforms.IsEmpty())
	{
		if (Transforms.Num() > MaxDuplicates)
		{
			RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("'transforms' must list at most %d entries, not %d."), MaxDuplicates, Transforms.Num()));
			return Result;
		}
		for (int32 Index = 0; Index < Transforms.Num(); ++Index)
		{
			const FString Label = FString::Printf(TEXT("transforms[%d]"), Index);
			const TSharedPtr<FJsonObject>& Entry = Transforms[Index].JsonObject;
			if (!Entry.IsValid())
			{
				Problems.Add(FString::Printf(TEXT("%s is not an object"), *Label), TEXT("INVALID_ARGUMENT"));
				continue;
			}
			FTransformFields Fields;
			if (ReadTransformFields(*Entry, Label, Problems, Fields))
			{
				Placements.Add(Fields);
			}
		}
		if (!Problems.IsEmpty())
		{
			Tools::RaiseProblems(TEXT("Nothing was duplicated"), Problems.Messages, Problems.Codes,
				TEXT("Each entry is {\"location\": [X, Y, Z], \"rotation\": [Pitch, Yaw, Roll], \"scale\": [X, Y, Z]}."));
			return Result;
		}
	}
	else
	{
		if (Count < 1 || Count > MaxDuplicates)
		{
			RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("'count' must be 1-%d, not %d."), MaxDuplicates, Count));
			return Result;
		}
		if (!bHasOffset)
		{
			Result.Warnings.Add(TEXT("Without an offset every copy sits on the original."));
		}
	}

	const int32 Copies = Placements.IsEmpty() ? Count : Placements.Num();
	const FTransform Source = Actor->GetActorTransform();
	for (int32 Index = 0; Index < Copies; ++Index)
	{
		const FVector Step = Placements.IsEmpty() ? OffsetStep * static_cast<double>(Index + 1) : FVector::ZeroVector;
		AActor* Copy = Subsystem->DuplicateActor(Actor, World, Step);
		if (!IsValid(Copy))
		{
			RaiseToolError(TEXT("DUPLICATE_FAILED"), FString::Printf(TEXT("%s could not be duplicated."), *Actor->GetPathName()), TEXT("log_get_recent may show the reason."));
			return Result;
		}
		if (!Placements.IsEmpty() && Copy->GetRootComponent())
		{
			Copy->SetActorTransform(Placements[Index].Apply(Source), /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
		}
		Copy->PostEditMove(/*bFinished=*/true);
		Result.Actors.Add(MakeSummary(Copy));
	}

	Result.bApplied = true;
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
	return Result;
}

FAgentMcpActorBatchResult UAgentMcpActorTools::Delete(const TArray<FString>& Actors, bool bConfirm)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::ActorWriteToolsPrivate;

	FAgentMcpActorBatchResult Result;
	UWorld* World = RequireEditorWorld();
	UEditorActorSubsystem* Subsystem = World ? RequireActorSubsystem() : nullptr;
	if (!Subsystem)
	{
		return Result;
	}
	if (Actors.IsEmpty())
	{
		RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("'actors' must name at least one actor."), TEXT("actor_find lists the actors of the level."));
		return Result;
	}

	FProblems Problems;
	TArray<AActor*> Targets;
	TSet<const AActor*> Visited;
	for (int32 Index = 0; Index < Actors.Num(); ++Index)
	{
		AActor* Actor = FindEditorActor(World, Actors[Index], FString::Printf(TEXT("actors[%d]"), Index), Problems);
		if (Actor && !Visited.Contains(Actor))
		{
			Visited.Add(Actor);
			Targets.Add(Actor);
		}
	}
	if (!Problems.IsEmpty())
	{
		Tools::RaiseProblems(TEXT("Nothing was deleted"), Problems.Messages, Problems.Codes, TEXT("Pass object paths from actor_find, or actor labels."));
		return Result;
	}

	// EditorDestroyActor only detaches what hangs off an actor, so the attached actors are collected and deleted with it.
	TArray<AActor*> Attached;
	for (const AActor* Target : Targets)
	{
		CollectAttached(Target, Visited, Attached);
	}
	for (const AActor* Child : Attached)
	{
		Result.AttachedActors.Add(Child->GetPathName());
	}
	for (const AActor* Target : Targets)
	{
		Result.Actors.Add(MakeSummary(Target));
	}

	if (!bConfirm)
	{
		Result.Warnings.Add(TEXT("Dry run: nothing was deleted. Call again with bConfirm true to delete these actors."));
		return Result;
	}

	TArray<AActor*> ToDestroy = Targets;
	ToDestroy.Append(Attached);
	if (!Subsystem->DestroyActors(ToDestroy))
	{
		RaiseToolError(TEXT("DELETE_FAILED"), TEXT("The actors could not be deleted."), TEXT("A locked level or a referenced actor can refuse; log_get_recent may show the reason."));
		return Result;
	}
	Result.bApplied = true;
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
	return Result;
}

FAgentMcpActorAttachResult UAgentMcpActorTools::Attach(AActor* Child, AActor* Parent, const FString& Socket, bool bKeepWorldTransform)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::ActorWriteToolsPrivate;

	FAgentMcpActorAttachResult Result;
	if (!Tools::RequireObject(Child, TEXT("child")) || !Tools::RequireObject(Parent, TEXT("parent")) || !GEditor)
	{
		return Result;
	}
	if (!IsEditorLevelActor(Child) || !IsEditorLevelActor(Parent))
	{
		RaiseToolError(TEXT("NOT_SUPPORTED"), TEXT("Both actors must be in the editor level."),
			TEXT("Stop the play session and address the actors in the editor level (actor_find with world Editor)."));
		return Result;
	}

	FText Reason;
	if (!GEditor->CanParentActors(Parent, Child, &Reason))
	{
		RaiseToolError(TEXT("ATTACH_REFUSED"), FString::Printf(TEXT("%s cannot be attached to %s: %s"), *Child->GetActorLabel(), *Parent->GetActorLabel(), *Reason.ToString()));
		return Result;
	}

	const FString SocketName = Socket.TrimStartAndEnd();
	if (!SocketName.IsEmpty() && SocketName.Len() >= NAME_SIZE)
	{
		RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("'socket' is too long."));
		return Result;
	}

	const USceneComponent* ChildRoot = Child->GetRootComponent();
	const FTransform RelativeBefore = ChildRoot ? ChildRoot->GetRelativeTransform() : FTransform::Identity;

	// ParentActors keeps the world transform, which is what the World Outliner does when an actor is dragged onto another.
	GEditor->ParentActors(Parent, Child, SocketName.IsEmpty() ? NAME_None : FName(*SocketName));
	if (Child->GetAttachParentActor() != Parent)
	{
		RaiseToolError(TEXT("ATTACH_FAILED"), FString::Printf(TEXT("%s was not attached to %s."), *Child->GetActorLabel(), *Parent->GetActorLabel()),
			TEXT("log_get_recent may show the reason."));
		return Result;
	}
	if (!bKeepWorldTransform)
	{
		// Reading the old relative values against the new parent is what KeepRelativeTransform does, so the actor moves.
		if (USceneComponent* Root = Child->GetRootComponent())
		{
			Root->SetRelativeTransform(RelativeBefore);
		}
	}
	Child->PostEditMove(/*bFinished=*/true);

	Result.Child = Child->GetPathName();
	Result.Parent = Parent->GetPathName();
	Result.Socket = SocketName;
	Result.Transform = Tools::MakeTransformValue(Child->GetActorTransform());
	return Result;
}

FAgentMcpActorBatchResult UAgentMcpActorTools::SetFolder(const TArray<FString>& Actors, const FString& Folder)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::ActorWriteToolsPrivate;

	FAgentMcpActorBatchResult Result;
	UWorld* World = RequireEditorWorld();
	if (!World)
	{
		return Result;
	}
	if (Actors.IsEmpty())
	{
		RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("'actors' must name at least one actor."), TEXT("actor_find lists the actors of the level."));
		return Result;
	}
	FString FolderPath = Folder.TrimStartAndEnd();
	FolderPath.RemoveFromEnd(TEXT("/"));
	if (FolderPath.Len() >= NAME_SIZE)
	{
		RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("'folder' is too long."));
		return Result;
	}

	FProblems Problems;
	TArray<AActor*> Targets;
	for (int32 Index = 0; Index < Actors.Num(); ++Index)
	{
		if (AActor* Actor = FindEditorActor(World, Actors[Index], FString::Printf(TEXT("actors[%d]"), Index), Problems))
		{
			Targets.AddUnique(Actor);
		}
	}
	if (!Problems.IsEmpty())
	{
		Tools::RaiseProblems(TEXT("No actor was moved"), Problems.Messages, Problems.Codes, TEXT("Pass object paths from actor_find, or actor labels."));
		return Result;
	}

	const FName FolderName = FolderPath.IsEmpty() ? NAME_None : FName(*FolderPath);
	for (AActor* Actor : Targets)
	{
		Actor->Modify();
		// Attached actors follow their parent in the outliner, as they do when the folder is changed by hand.
		Actor->SetFolderPath_Recursively(FolderName);
		Result.Actors.Add(MakeSummary(Actor));
	}
	Result.bApplied = true;
	return Result;
}
