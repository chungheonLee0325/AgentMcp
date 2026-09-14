#include "AgentMcpObjectTools.h"

#include "AgentMcpJson.h"
#include "AgentMcpToolsCommon.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "UObject/PropertyAccessUtil.h"
#include "UObject/UnrealType.h"

namespace UE::AgentMcp::ObjectToolsPrivate
{
	constexpr int32 MaxListLimit = 500;
	constexpr int32 MaxDescriptionLength = 200;

	/** A property is readable when it is exposed to the editor or to Blueprints, as for Python get_editor_property. */
	bool IsUserVisible(const FProperty* Property)
	{
		return PropertyAccessUtil::CanGetPropertyValue(Property) == EPropertyAccessResultFlags::Success;
	}

	/** This version changes objects of the editor level only: actors, their components and other level sub-objects. */
	bool RequireEditableLevelObject(const UObject* Object)
	{
		if (Object->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
		{
			RaiseToolError(TEXT("NOT_SUPPORTED"), FString::Printf(TEXT("%s is a class default or archetype object."), *Object->GetPathName()),
				TEXT("Blueprint and class defaults cannot be changed with object_set_properties."));
			return false;
		}

		const ULevel* Level = Object->IsA<ULevel>() ? CastChecked<ULevel>(Object) : Object->GetTypedOuter<ULevel>();
		const UWorld* World = Level ? Level->GetTypedOuter<UWorld>() : nullptr;
		if (!World)
		{
			RaiseToolError(TEXT("NOT_SUPPORTED"), FString::Printf(TEXT("%s is not part of a level."), *Object->GetPathName()),
				TEXT("object_set_properties changes actors and components in the editor level; asset writes are not enabled yet."));
			return false;
		}
		if (World->IsGameWorld())
		{
			RaiseToolError(TEXT("NOT_SUPPORTED"), FString::Printf(TEXT("%s belongs to a play session."), *Object->GetPathName()),
				TEXT("Play session objects are discarded when the session ends; change the object in the editor level instead."));
			return false;
		}
		if (!Object->CanModify())
		{
			RaiseToolError(TEXT("NOT_EDITABLE"), FString::Printf(TEXT("%s cannot be modified, for example because its level is locked."), *Object->GetPathName()));
			return false;
		}
		return true;
	}
}

FAgentMcpPropertyListResult UAgentMcpObjectTools::ListProperties(UObject* Object, const FString& Filter, bool bUserVisibleOnly, int32 Limit, int32 Cursor)
{
	using namespace UE::AgentMcp::ObjectToolsPrivate;

	FAgentMcpPropertyListResult Result;
	if (!UE::AgentMcp::Tools::RequireObject(Object, TEXT("object")))
	{
		return Result;
	}
	Result.Object = Object->GetPathName();
	Result.ClassName = Object->GetClass()->GetName();

	const FString FilterText = Filter.TrimStartAndEnd();
	TArray<const FProperty*> Matches;
	for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
	{
		const FProperty* Property = *It;
		if (Property->HasAnyPropertyFlags(CPF_Deprecated) || (bUserVisibleOnly && !IsUserVisible(Property)))
		{
			continue;
		}
		if (!FilterText.IsEmpty() && !Property->GetName().Contains(FilterText) && !Property->GetMetaData(TEXT("Category")).Contains(FilterText))
		{
			continue;
		}
		Matches.Add(Property);
	}

	int32 Start = 0;
	int32 End = 0;
	int32 NextCursor = -1;
	if (!UE::AgentMcp::Tools::GetPage(Matches.Num(), Limit, Cursor, MaxListLimit, Start, End, NextCursor))
	{
		return Result;
	}
	Result.TotalMatched = Matches.Num();
	Result.NextCursor = NextCursor;

	for (int32 Index = Start; Index < End; ++Index)
	{
		const FProperty* Property = Matches[Index];
		FAgentMcpPropertyInfo& Info = Result.Properties.AddDefaulted_GetRef();
		Info.Name = Property->GetName();
		FString ExtendedType;
		Info.Type = Property->GetCPPType(&ExtendedType) + ExtendedType;
		Info.Category = Property->GetMetaData(TEXT("Category"));
		Info.DeclaredIn = GetNameSafe(Property->GetOwnerStruct());
		Info.NotEditableReason = UE::AgentMcp::Tools::GetPropertyNotEditableReason(Object, Property);
		Info.bEditable = Info.NotEditableReason.IsEmpty();

		FString Tooltip = Property->GetMetaData(TEXT("ToolTip"));
		int32 LineBreak = INDEX_NONE;
		if (Tooltip.FindChar(TEXT('\n'), LineBreak))
		{
			Tooltip.LeftInline(LineBreak);
		}
		Info.Description = Tooltip.Left(MaxDescriptionLength).TrimStartAndEnd();
	}
	return Result;
}

FAgentMcpPropertyValuesResult UAgentMcpObjectTools::GetProperties(UObject* Object, const TArray<FString>& PropertyNames)
{
	using namespace UE::AgentMcp::ObjectToolsPrivate;

	FAgentMcpPropertyValuesResult Result;
	if (!UE::AgentMcp::Tools::RequireObject(Object, TEXT("object")))
	{
		return Result;
	}
	Result.Object = Object->GetPathName();

	const TSharedRef<FJsonObject> Values = MakeShared<FJsonObject>();
	if (PropertyNames.IsEmpty())
	{
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			if (!It->HasAnyPropertyFlags(CPF_Deprecated) && IsUserVisible(*It) && UE::AgentMcp::IsJsonCompatibleProperty(*It))
			{
				Values->SetField(It->GetName(), UE::AgentMcp::Tools::ReadPropertyValue(Object, *It));
			}
		}
	}
	else
	{
		for (const FString& RequestedName : PropertyNames)
		{
			const FProperty* Property = UE::AgentMcp::Tools::FindPropertyByName(Object, RequestedName);
			if (!Property)
			{
				Result.Missing.Add(RequestedName);
			}
			else if (!UE::AgentMcp::Tools::IsPropertyStoredOnObject(Property, Object) || !IsUserVisible(Property) || !UE::AgentMcp::IsJsonCompatibleProperty(Property))
			{
				Result.Inaccessible.Add(Property->GetName());
			}
			else
			{
				Values->SetField(Property->GetName(), UE::AgentMcp::Tools::ReadPropertyValue(Object, Property));
			}
		}
	}

	Result.Values.JsonObject = Values;
	return Result;
}

FAgentMcpSetPropertiesResult UAgentMcpObjectTools::SetProperties(UObject* Object, const FJsonObjectWrapper& Values)
{
	using namespace UE::AgentMcp::ObjectToolsPrivate;
	using UE::AgentMcp::Tools::FPreparedPropertyValues;

	FAgentMcpSetPropertiesResult Result;
	if (!UE::AgentMcp::Tools::RequireObject(Object, TEXT("object")) || !RequireEditableLevelObject(Object))
	{
		return Result;
	}
	const FString ObjectPath = Object->GetPathName();
	Result.Object = ObjectPath;

	const TSharedPtr<FJsonObject>& Requested = Values.JsonObject;
	if (!Requested.IsValid() || Requested->Values.IsEmpty())
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("'values' must contain at least one property."), TEXT("object_list_properties lists the editable properties."));
		return Result;
	}

	// Check and convert every value before changing anything, so a bad value leaves the object untouched.
	FPreparedPropertyValues Prepared;
	TArray<FString> Problems;
	TSet<FString> ProblemCodes;
	if (!Prepared.Prepare(Object, *Requested, Problems, ProblemCodes))
	{
		UE::AgentMcp::Tools::RaiseProblems(FString::Printf(TEXT("Nothing was changed on %s"), *ObjectPath), Problems, ProblemCodes,
			TEXT("object_list_properties shows property names, types and whether each property is editable."));
		return Result;
	}

	const TSharedRef<FJsonObject> Before = MakeShared<FJsonObject>();
	for (const FPreparedPropertyValues::FEntry& Entry : Prepared.GetEntries())
	{
		Before->SetField(Entry.Property->GetName(), UE::AgentMcp::Tools::ReadPropertyValue(Object, Entry.Property));
	}

	UObject* Target = Object;
	for (const FPreparedPropertyValues::FEntry& Entry : Prepared.GetEntries())
	{
		const FString PropertyName = Entry.Property->GetName();
		if (PropertyAccessUtil::IsCompletePropertyIdentical(Entry.Property, Entry.Value, Entry.Property, Entry.Property->ContainerPtrToValuePtr<void>(Target)))
		{
			Result.Unchanged.Add(PropertyName);
			continue;
		}

		// Record the object for undo; PropertyAccessUtil then emits PreEditChange and PostEditChangeChainProperty around the copy.
		Target->Modify();
		const EPropertyAccessResultFlags SetResult = PropertyAccessUtil::SetPropertyValue_Object(
			Entry.Property, Target, Entry.Property, Entry.Value, INDEX_NONE, PropertyAccessUtil::EditorReadOnlyFlags, EPropertyAccessChangeNotifyMode::Default);
		if (SetResult != EPropertyAccessResultFlags::Success)
		{
			// Properties changed before this one are rolled back with the dispatcher transaction.
			UE::AgentMcp::RaiseToolError(TEXT("PROPERTY_WRITE_FAILED"), FString::Printf(TEXT("Setting '%s' on %s failed."), *PropertyName, *ObjectPath));
			return Result;
		}
		Result.Changed.Add(PropertyName);

		// A change can rerun construction scripts, which can replace components created by them. Continue with the replacement.
		if (!IsValid(Target))
		{
			Target = StaticFindObject(UObject::StaticClass(), nullptr, *ObjectPath);
			if (!IsValid(Target) || !Target->IsA(Entry.Property->GetOwnerClass()))
			{
				UE::AgentMcp::RaiseToolError(TEXT("OBJECT_REPLACED"),
					FString::Printf(TEXT("%s was replaced while '%s' was being set, so the change could not be verified."), *ObjectPath, *PropertyName),
					TEXT("The change was rolled back. Look the object up again (actor_inspect) and retry."));
				return Result;
			}
		}
	}

	const TSharedRef<FJsonObject> After = MakeShared<FJsonObject>();
	for (const FPreparedPropertyValues::FEntry& Entry : Prepared.GetEntries())
	{
		After->SetField(Entry.Property->GetName(), UE::AgentMcp::Tools::ReadPropertyValue(Target, Entry.Property));
	}

	if (Result.Changed.Num() > 0 && GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}

	Result.Before.JsonObject = Before;
	Result.After.JsonObject = After;
	return Result;
}
