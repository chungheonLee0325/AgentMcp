#include "AgentMcpToolsCommon.h"

#include "AgentMcpToolset.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Interfaces/IPluginManager.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "UObject/Package.h"

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
}
