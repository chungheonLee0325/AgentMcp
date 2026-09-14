#include "AgentMcpToolsCommon.h"

#include "AgentMcpJson.h"
#include "AgentMcpSettings.h"
#include "AgentMcpToolset.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "UObject/Package.h"
#include "UObject/PropertyAccessUtil.h"
#include "UObject/UnrealType.h"

namespace UE::AgentMcp::Tools
{
	namespace CommonPrivate
	{
		/** Rounds floating point noise such as 1e-13 away so results stay readable. */
		double RoundForOutput(double Value)
		{
			const double Rounded = FMath::RoundToDouble(Value * 10000.0) / 10000.0;
			return Rounded == 0.0 ? 0.0 : Rounded;
		}
	}

	UWorld* ResolveWorld(EAgentMcpWorld World)
	{
		if (!GEditor)
		{
			RaiseToolError(TEXT("EDITOR_UNAVAILABLE"), TEXT("GEditor is not available."));
			return nullptr;
		}

		if (World == EAgentMcpWorld::Play)
		{
			UWorld* PlayWorld = GEditor->PlayWorld.Get();
			if (!PlayWorld)
			{
				RaiseToolError(TEXT("PIE_NOT_ACTIVE"), TEXT("No play session is running."), TEXT("Start one with pie_start, or search the editor level (world Editor)."));
			}
			return PlayWorld;
		}

		UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
		if (!EditorWorld)
		{
			RaiseToolError(TEXT("EDITOR_UNAVAILABLE"), TEXT("No level is open in the editor."));
		}
		return EditorWorld;
	}

	FString GetWorldKind(const UWorld* World)
	{
		return World && World->IsPlayInEditor() ? TEXT("Play") : TEXT("Editor");
	}

	FAgentMcpPlaySessionState MakePlaySessionState()
	{
		FAgentMcpPlaySessionState State;
		if (!GEditor)
		{
			return State;
		}

		const UWorld* PlayWorld = GEditor->PlayWorld.Get();
		State.bActive = PlayWorld != nullptr || GEditor->IsPlaySessionInProgress();
		State.bBegunPlay = PlayWorld != nullptr && PlayWorld->HasBegunPlay();
		State.bStarting = State.bActive && !State.bBegunPlay;
		State.bSimulating = GEditor->bIsSimulatingInEditor;
		State.bPaused = PlayWorld != nullptr && PlayWorld->bDebugPauseExecution;
		if (PlayWorld)
		{
			State.World = PlayWorld->GetPackage()->GetName();
			State.WorldTimeSeconds = CommonPrivate::RoundForOutput(PlayWorld->GetTimeSeconds());
		}
		return State;
	}

	FAgentMcpTransformValue MakeTransformValue(const FTransform& Transform)
	{
		using CommonPrivate::RoundForOutput;

		const FVector Location = Transform.GetLocation();
		const FRotator Rotation = Transform.Rotator();
		const FVector Scale = Transform.GetScale3D();

		FAgentMcpTransformValue Value;
		Value.Location = { RoundForOutput(Location.X), RoundForOutput(Location.Y), RoundForOutput(Location.Z) };
		Value.Rotation = { RoundForOutput(Rotation.Pitch), RoundForOutput(Rotation.Yaw), RoundForOutput(Rotation.Roll) };
		Value.Scale = { RoundForOutput(Scale.X), RoundForOutput(Scale.Y), RoundForOutput(Scale.Z) };
		return Value;
	}

	bool ReadOptionalVector(const TArray<double>& Values, const TCHAR* ArgumentName, bool& bOutProvided, FVector& OutVector)
	{
		bOutProvided = Values.Num() > 0;
		if (!bOutProvided)
		{
			return true;
		}
		if (Values.Num() != 3)
		{
			RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("'%s' must hold exactly 3 numbers, not %d."), ArgumentName, Values.Num()));
			return false;
		}
		for (const double Component : Values)
		{
			if (!FMath::IsFinite(Component))
			{
				RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("'%s' must hold finite numbers."), ArgumentName));
				return false;
			}
		}
		OutVector = FVector(Values[0], Values[1], Values[2]);
		return true;
	}

	bool RequireObject(const UObject* Object, const TCHAR* ArgumentName)
	{
		if (Object)
		{
			return true;
		}
		RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("'%s' is required and cannot be null."), ArgumentName));
		return false;
	}

	bool GetPage(int32 Total, int32 Limit, int32 Cursor, int32 MaxLimit, int32& OutStart, int32& OutEnd, int32& OutNextCursor)
	{
		if (Cursor < 0 || Cursor > Total)
		{
			RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("'cursor' %d is outside the result (0 to %d)."), Cursor, Total),
				TEXT("Pass 0 for the first page, or nextCursor of the previous call."));
			return false;
		}
		const int32 PageSize = FMath::Clamp(Limit, 1, MaxLimit);
		OutStart = Cursor;
		OutEnd = FMath::Min(Total, Cursor + PageSize);
		OutNextCursor = OutEnd < Total ? OutEnd : -1;
		return true;
	}

	bool MatchesNameFilter(const FString& Value, const FString& Pattern)
	{
		if (Pattern.IsEmpty())
		{
			return true;
		}
		const bool bWildcard = Pattern.Contains(TEXT("*")) || Pattern.Contains(TEXT("?"));
		return bWildcard ? Value.MatchesWildcard(Pattern) : Value.Contains(Pattern);
	}

	FString StripExportTextPath(const FString& Text)
	{
		FString Result = Text.TrimStartAndEnd();
		int32 QuoteIndex = INDEX_NONE;
		if (Result.EndsWith(TEXT("'")) && Result.FindChar(TEXT('\''), QuoteIndex) && QuoteIndex < Result.Len() - 1)
		{
			Result = Result.Mid(QuoteIndex + 1, Result.Len() - QuoteIndex - 2);
		}
		return Result;
	}

	const UClass* FindNativeClass(const UClass* Class)
	{
		while (Class && !Class->HasAnyClassFlags(CLASS_Native))
		{
			Class = Class->GetSuperClass();
		}
		return Class;
	}

	FAgentMcpNativeClassInfo MakeNativeClassInfo(const UClass* NativeClass)
	{
		FAgentMcpNativeClassInfo Info;
		if (NativeClass)
		{
			Info.ClassPath = NativeClass->GetPathName();
			Info.Module = NativeClass->GetPackage()->GetName();
			Info.Header = NativeClass->GetMetaData(TEXT("ModuleRelativePath"));
		}
		return Info;
	}

	bool IsProjectContentPackage(const FString& PackageName)
	{
		if (PackageName.StartsWith(TEXT("/Game/")))
		{
			return true;
		}
		if (PackageName.IsEmpty() || FPackageName::IsScriptPackage(PackageName))
		{
			return false;
		}
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPluginFromPath(PackageName);
		return Plugin.IsValid() && Plugin->GetLoadedFrom() == EPluginLoadedFrom::Project;
	}

	bool RequireProjectContent(const UObject* Asset)
	{
		const UPackage* Package = Asset ? Asset->GetPackage() : nullptr;
		const bool bProjectContent = Package && IsProjectContentPackage(Package->GetName());
		if (!bProjectContent)
		{
			RaiseToolError(TEXT("NOT_SUPPORTED"), FString::Printf(TEXT("%s is not project content."), *GetPathNameSafe(Asset)),
				TEXT("Tools change assets under /Game or in project plugins only; engine content is read-only for tools."));
		}
		return bProjectContent;
	}

	bool IsPlaySessionActive()
	{
		return GEditor && (GEditor->PlayWorld != nullptr || GEditor->IsPlaySessionInProgress());
	}

	FString GetNewAssetPathProblem(const FString& AssetPath, FString& OutPackageName, FString& OutAssetName, FString& OutCode)
	{
		OutCode = TEXT("INVALID_ARGUMENT");
		const FString Path = AssetPath.TrimStartAndEnd();
		FString PackageName = Path;
		int32 DotIndex = INDEX_NONE;
		if (Path.FindLastChar(TEXT('.'), DotIndex))
		{
			PackageName = Path.Left(DotIndex);
			if (Path.Mid(DotIndex + 1) != FPackageName::GetShortName(PackageName))
			{
				return FString::Printf(TEXT("'%s': the asset name after the '.' must match the last part of the path"), *Path.Left(256));
			}
		}

		FText InvalidReason;
		if (!FPackageName::IsValidLongPackageName(PackageName, /*bIncludeReadOnlyRoots=*/false, &InvalidReason))
		{
			FString Reason = InvalidReason.ToString();
			Reason.RemoveFromEnd(TEXT("."));
			return FString::Printf(TEXT("'%s' is not a valid package path: %s"), *PackageName.Left(256), *Reason);
		}
		const FString AssetName = FPackageName::GetShortName(PackageName);
		if (!FName::IsValidXName(AssetName, INVALID_OBJECTNAME_CHARACTERS))
		{
			return FString::Printf(TEXT("'%s' is not a valid asset name; use letters, digits and underscores"), *AssetName);
		}
		if (!IsProjectContentPackage(PackageName))
		{
			OutCode = TEXT("NOT_SUPPORTED");
			return FString::Printf(TEXT("%s is not project content; tools create assets under /Game or in project plugins only"), *PackageName);
		}

		OutCode.Reset();
		OutPackageName = PackageName;
		OutAssetName = AssetName;
		return FString();
	}

	bool DoesAssetExist(const FString& PackageName, const FString& AssetName)
	{
		if (UPackage* Package = FindPackage(nullptr, *PackageName))
		{
			if (IsValid(StaticFindObjectFast(UObject::StaticClass(), Package, FName(*AssetName))))
			{
				return true;
			}
		}
		if (FPackageName::DoesPackageExist(PackageName))
		{
			return true;
		}
		TArray<FAssetData> Assets;
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetsByPackageName(FName(*PackageName), Assets);
		return !Assets.IsEmpty();
	}

	TArray<FString> GetSourceControlWarnings(const UPackage* Package)
	{
		TArray<FString> Warnings;
		ISourceControlModule& SourceControl = ISourceControlModule::Get();
		if (!Package || !SourceControl.IsEnabled())
		{
			return Warnings;
		}

		const FSourceControlStatePtr State = SourceControl.GetProvider().GetState(Package, EStateCacheUsage::Use);
		if (!State.IsValid())
		{
			return Warnings;
		}
		FString OtherUser;
		if (State->IsCheckedOutOther(&OtherUser))
		{
			Warnings.Add(FString::Printf(TEXT("%s is checked out by %s; saving it would conflict."), *Package->GetName(), *OtherUser));
		}
		else if (State->IsSourceControlled() && !State->IsCheckedOut() && !State->IsAdded())
		{
			Warnings.Add(FString::Printf(TEXT("%s is not checked out; check it out before saving."), *Package->GetName()));
		}
		return Warnings;
	}

	FString GetShortDescription(const FString& ToolTip, int32 MaxLength)
	{
		FString Description = ToolTip;
		int32 LineBreak = INDEX_NONE;
		if (Description.FindChar(TEXT('\n'), LineBreak))
		{
			Description.LeftInline(LineBreak);
		}
		return Description.Left(MaxLength).TrimStartAndEnd();
	}

	namespace CommonPrivate
	{
		bool IsBlockedBySettings(const UObject* Object, const FProperty* Property)
		{
			const TArray<FString>& Patterns = GetDefault<UAgentMcpSettings>()->BlockedProperties;
			if (Patterns.IsEmpty())
			{
				return false;
			}

			const FString PropertyName = Property->GetName();
			for (const UClass* Class = Object->GetClass(); Class; Class = Class->GetSuperClass())
			{
				const FString QualifiedName = Class->GetName() + TEXT(".") + PropertyName;
				for (const FString& Pattern : Patterns)
				{
					if (QualifiedName.MatchesWildcard(Pattern))
					{
						return true;
					}
				}
			}
			return false;
		}
	}

	bool IsPropertyStoredOnObject(const FProperty* Property, const UObject* Object)
	{
		const UClass* OwnerClass = Property->GetOwnerClass();
		return OwnerClass && Object->IsA(OwnerClass);
	}

	FProperty* FindPropertyByName(const UObject* Object, const FString& PropertyName)
	{
		const FString Trimmed = PropertyName.TrimStartAndEnd();
		if (!Object || Trimmed.IsEmpty() || Trimmed.Len() >= NAME_SIZE)
		{
			return nullptr;
		}
		// A name that was never created cannot name a property, so look it up without adding it to the name table.
		const FName LookupName(*Trimmed, FNAME_Find);
		return LookupName.IsNone() ? nullptr : PropertyAccessUtil::FindPropertyByName(LookupName, Object->GetClass());
	}

	FString GetPropertyNotEditableReason(const UObject* Object, const FProperty* Property, bool bCheckAsInstance)
	{
		if (Property->HasAnyPropertyFlags(CPF_Deprecated))
		{
			return TEXT("it is deprecated");
		}
		if (!IsJsonCompatibleProperty(Property))
		{
			return TEXT("its type cannot be exchanged as JSON");
		}
		if (!IsPropertyStoredOnObject(Property, Object))
		{
			return TEXT("it is sparse class data");
		}
		if (Property->HasAnyPropertyFlags(CPF_InstancedReference | CPF_PersistentInstance | CPF_ContainsInstancedReference))
		{
			return TEXT("it references instanced sub-objects");
		}

		const bool bIsTemplate = !bCheckAsInstance && PropertyAccessUtil::IsObjectTemplate(Object);
		const EPropertyAccessResultFlags Access = PropertyAccessUtil::CanSetPropertyValue(Property, PropertyAccessUtil::EditorReadOnlyFlags, bIsTemplate);
		if (EnumHasAnyFlags(Access, EPropertyAccessResultFlags::AccessProtected))
		{
			return TEXT("it is not exposed to the editor or Blueprint");
		}
		if (EnumHasAnyFlags(Access, EPropertyAccessResultFlags::CannotEditTemplate))
		{
			return TEXT("it can only be edited on placed instances (EditInstanceOnly)");
		}
		if (EnumHasAnyFlags(Access, EPropertyAccessResultFlags::CannotEditInstance))
		{
			return TEXT("it can only be edited on class defaults (EditDefaultsOnly)");
		}
		if (EnumHasAnyFlags(Access, EPropertyAccessResultFlags::ReadOnly))
		{
			return TEXT("it is read-only (VisibleAnywhere or EditConst)");
		}
		if (Access != EPropertyAccessResultFlags::Success)
		{
			return TEXT("it is not editable");
		}
		if (CommonPrivate::IsBlockedBySettings(Object, Property))
		{
			return TEXT("it matches the BlockedProperties setting");
		}
		if (!Object->CanEditChange(Property))
		{
			return TEXT("the object does not allow editing it in its current state (CanEditChange)");
		}
		return FString();
	}

	TSharedRef<FJsonValue> ReadPropertyValue(const UObject* Object, const FProperty* Property)
	{
		const TSharedPtr<FJsonValue> Value = PropertyValueToJson(Property, Property->ContainerPtrToValuePtr<void>(Object));
		if (Value.IsValid())
		{
			return Value.ToSharedRef();
		}
		return MakeShared<FJsonValueNull>();
	}

	FPreparedPropertyValues::~FPreparedPropertyValues()
	{
		for (const FEntry& Entry : Entries)
		{
			Entry.Property->DestroyValue(Entry.Value);
			FMemory::Free(Entry.Value);
		}
	}

	bool FPreparedPropertyValues::Prepare(const UObject* Object, const FJsonObject& Values, TArray<FString>& OutProblems, TSet<FString>& OutProblemCodes, bool bCheckAsInstance)
	{
		const int32 ProblemsBefore = OutProblems.Num();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Values.Values)
		{
			FProperty* Property = FindPropertyByName(Object, Pair.Key);
			if (!Property)
			{
				OutProblems.Add(FString::Printf(TEXT("'%s' is not a property of %s"), *Pair.Key.Left(128), *Object->GetClass()->GetName()));
				OutProblemCodes.Add(TEXT("UNKNOWN_PROPERTY"));
				continue;
			}

			const FString Reason = GetPropertyNotEditableReason(Object, Property, bCheckAsInstance);
			if (!Reason.IsEmpty())
			{
				OutProblems.Add(FString::Printf(TEXT("'%s' cannot be changed: %s"), *Property->GetName(), *Reason));
				OutProblemCodes.Add(TEXT("PROPERTY_NOT_EDITABLE"));
				continue;
			}

			// Start from the current value so struct fields missing from the JSON keep their value.
			FEntry& Entry = Entries.AddDefaulted_GetRef();
			Entry.Property = Property;
			Entry.Value = FMemory::Malloc(static_cast<SIZE_T>(FMath::Max(Property->GetSize(), 1)), static_cast<uint32>(Property->GetMinAlignment()));
			Property->InitializeValue(Entry.Value);
			Property->GetValue_InContainer(Object, Entry.Value);

			FString ErrorCode;
			FString Error;
			if (!JsonToPropertyValue(Pair.Value, Property, Entry.Value, Property->GetName(), ErrorCode, Error))
			{
				// Problems are joined into one sentence, so drop their own final period.
				Error.RemoveFromEnd(TEXT("."));
				OutProblems.Add(Error);
				OutProblemCodes.Add(ErrorCode);
			}
		}
		return OutProblems.Num() == ProblemsBefore;
	}

	void RaiseProblems(const FString& Summary, const TArray<FString>& Problems, const TSet<FString>& ProblemCodes, const FString& Hint)
	{
		const FString Code = ProblemCodes.Num() == 1 ? *ProblemCodes.CreateConstIterator() : FString(TEXT("INVALID_ARGUMENT"));
		RaiseToolError(Code, FString::Printf(TEXT("%s: %s."), *Summary, *FString::Join(Problems, TEXT("; "))), Hint);
	}
}
