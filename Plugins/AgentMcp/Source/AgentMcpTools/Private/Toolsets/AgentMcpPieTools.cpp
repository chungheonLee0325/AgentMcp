#include "AgentMcpPieTools.h"

#include "AgentMcpLogBuffer.h"
#include "AgentMcpToolsCommon.h"

#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "IAssetViewport.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "PlayInEditorDataTypes.h"
#include "Slate/SceneViewport.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

namespace UE::AgentMcp::PieToolsPrivate
{
	constexpr int32 MaxListedBlueprints = 10;
	constexpr int32 MinWindowWidth = 320;
	constexpr int32 MaxWindowWidth = 7680;
	constexpr int32 MinWindowHeight = 240;
	constexpr int32 MaxWindowHeight = 4320;
	constexpr int32 MaxWindowResizeAttempts = 3;
	constexpr double WindowResizeWaitSeconds = 2.0;

	int64 GetLatestLogSequence()
	{
		return static_cast<int64>(FAgentMcpLogBuffer::Get().GetLatestSequence());
	}

	/**
	 * Blueprints that would stop PIE startup on a modal dialog, which would also block every tool call.
	 * Uses the conditions under which UE 5.5 PlayLevel.cpp shows those dialogs (FInternalPlayLevelUtils::ResolveDirtyBlueprints, ShowBlueprintErrorDialog).
	 */
	void FindBlockingBlueprints(TArray<FString>& OutWithErrors, TArray<FString>& OutNeedingCompile)
	{
		const bool bAutoRecompile = GetDefault<ULevelEditorPlaySettings>()->AutoRecompileBlueprints;
		for (TObjectIterator<UBlueprint> It; It; ++It)
		{
			const UBlueprint* Blueprint = *It;
			if (!IsValid(Blueprint) || Blueprint->IsUpToDate())
			{
				continue;
			}

			const bool bNeedsCompile = !FBlueprintEditorUtils::IsDataOnlyBlueprint(Blueprint) && Blueprint->IsPossiblyDirty() && Blueprint->Status != BS_Unknown;
			if (bNeedsCompile)
			{
				if (!bAutoRecompile)
				{
					OutNeedingCompile.Add(Blueprint->GetPathName());
				}
			}
			else if (Blueprint->Status == BS_Error && Blueprint->bDisplayCompilePIEWarning)
			{
				OutWithErrors.Add(Blueprint->GetPathName());
			}
		}
	}

	FString JoinLimited(const TArray<FString>& Items)
	{
		TArray<FString> Listed;
		for (int32 Index = 0; Index < Items.Num() && Index < MaxListedBlueprints; ++Index)
		{
			Listed.Add(Items[Index]);
		}
		if (Items.Num() > MaxListedBlueprints)
		{
			Listed.Add(FString::Printf(TEXT("and %d more"), Items.Num() - MaxListedBlueprints));
		}
		return FString::Join(Listed, TEXT(", "));
	}

	struct FStartWait
	{
		double RequestTime = 0.0;
		double WarmupSeconds = 0.0;
		double TimeoutSeconds = 0.0;
		int64 StartLogSequence = 0;
		TOptional<double> BegunPlayTime;
		/** Viewport size of a new play window; zero when the session plays in the level viewport. */
		FIntPoint RequestedViewportSize = FIntPoint::ZeroValue;
		FIntPoint SizeBeforeResize = FIntPoint::ZeroValue;
		int32 ResizeAttempts = 0;
		double LastResizeTime = 0.0;
	};

	FIntPoint GetPlayViewportSize()
	{
		const FViewport* PlayViewport = GEditor ? GEditor->GetPIEViewport() : nullptr;
		return PlayViewport ? PlayViewport->GetSizeXY() : FIntPoint::ZeroValue;
	}

	/** The new window that shows PlayViewport. With several clients this is the window of the viewport that viewport_capture captures. */
	TSharedPtr<SWindow> FindPlayWindow(const FViewport* PlayViewport)
	{
		if (GEditor && PlayViewport)
		{
			for (const TPair<FName, FSlatePlayInEditorInfo>& Entry : GEditor->SlatePlayInEditorMap)
			{
				if (static_cast<const FViewport*>(Entry.Value.SlatePlayInEditorWindowViewport.Get()) == PlayViewport)
				{
					return Entry.Value.SlatePlayInEditorWindow.Pin();
				}
			}
		}
		return nullptr;
	}
}

UAgentMcpAsyncResult* UAgentMcpPieTools::Start(bool bSimulate, float WarmupSeconds, float TimeoutSeconds, int32 WindowWidth, int32 WindowHeight)
{
	using namespace UE::AgentMcp::PieToolsPrivate;

	if (!GEditor)
	{
		UE::AgentMcp::RaiseToolError(TEXT("EDITOR_UNAVAILABLE"), TEXT("GEditor is not available."));
		return nullptr;
	}
	const bool bNewWindow = WindowWidth != 0 || WindowHeight != 0;
	if (bNewWindow && bSimulate)
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("Simulate In Editor runs in the level viewport and takes no window size."),
			TEXT("Leave windowWidth and windowHeight at 0, or play without bSimulate."));
		return nullptr;
	}
	if (bNewWindow && (WindowWidth < MinWindowWidth || WindowWidth > MaxWindowWidth || WindowHeight < MinWindowHeight || WindowHeight > MaxWindowHeight))
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"),
			FString::Printf(TEXT("A play window of %d x %d pixels is outside %d-%d by %d-%d."), WindowWidth, WindowHeight, MinWindowWidth, MaxWindowWidth, MinWindowHeight, MaxWindowHeight),
			TEXT("Give both windowWidth and windowHeight, or leave both at 0 to play in the level viewport."));
		return nullptr;
	}
	if (GEditor->PlayWorld || GEditor->IsPlaySessionInProgress())
	{
		UE::AgentMcp::RaiseToolError(TEXT("PIE_ALREADY_ACTIVE"), TEXT("A play session is already starting or running."), TEXT("Use pie_status to inspect it, or pie_stop to end it first."));
		return nullptr;
	}

	TArray<FString> WithErrors;
	TArray<FString> NeedingCompile;
	FindBlockingBlueprints(WithErrors, NeedingCompile);
	if (WithErrors.Num() > 0)
	{
		UE::AgentMcp::RaiseToolError(TEXT("BLUEPRINT_COMPILE_ERRORS"), FString::Printf(TEXT("Blueprints have compile errors: %s."), *JoinLimited(WithErrors)),
			TEXT("Fix and compile them first. Otherwise the editor asks for confirmation in a modal dialog, which blocks tool calls."));
		return nullptr;
	}
	if (NeedingCompile.Num() > 0)
	{
		UE::AgentMcp::RaiseToolError(TEXT("BLUEPRINTS_NEED_COMPILE"), FString::Printf(TEXT("Blueprints were changed without being compiled: %s."), *JoinLimited(NeedingCompile)),
			TEXT("Compile them first, or enable Auto Recompile Blueprints in the Level Editor Play settings. Otherwise the editor asks in a modal dialog, which blocks tool calls."));
		return nullptr;
	}

	FRequestPlaySessionParams Params;
	Params.WorldType = bSimulate ? EPlaySessionWorldType::SimulateInEditor : EPlaySessionWorldType::PlayInEditor;
	Params.SessionDestination = EPlaySessionDestinationType::InProcess;
	if (bNewWindow)
	{
		// Without a destination viewport the session opens new windows with the sizes of these settings: the first client uses the new
		// window size, additional clients the client window size. The request copies the settings, so the editor's Play settings stay as they are.
		ULevelEditorPlaySettings* PlaySettings = NewObject<ULevelEditorPlaySettings>(GetTransientPackage());
		PlaySettings->NewWindowWidth = WindowWidth;
		PlaySettings->NewWindowHeight = WindowHeight;
		PlaySettings->CenterNewWindow = true;
		PlaySettings->SetClientWindowSize(FIntPoint(WindowWidth, WindowHeight));
		Params.EditorPlaySettings = PlaySettings;
	}
	// Otherwise play in the active level viewport, as the toolbar Play button does.
	else if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
	{
		if (const TSharedPtr<IAssetViewport> ActiveViewport = LevelEditorModule->GetFirstActiveViewport())
		{
			Params.DestinationSlateViewport = TWeakPtr<IAssetViewport>(ActiveViewport);
		}
	}

	const TSharedRef<FStartWait> Wait = MakeShared<FStartWait>();
	Wait->RequestTime = FPlatformTime::Seconds();
	Wait->WarmupSeconds = FMath::Clamp(WarmupSeconds, 0.0f, 60.0f);
	Wait->TimeoutSeconds = FMath::Clamp(TimeoutSeconds, 5.0f, 600.0f);
	Wait->StartLogSequence = GetLatestLogSequence();
	Wait->RequestedViewportSize = bNewWindow ? FIntPoint(WindowWidth, WindowHeight) : FIntPoint::ZeroValue;

	// The editor starts the session on its next tick; the result completes once the play world has begun play.
	GEditor->RequestPlaySession(Params);

	return UAgentMcpAsyncResult::Create(static_cast<float>(Wait->TimeoutSeconds + Wait->WarmupSeconds), [Wait](UAgentMcpAsyncResult& Result)
	{
		const double Now = FPlatformTime::Seconds();
		const UWorld* PlayWorld = GEditor ? GEditor->PlayWorld.Get() : nullptr;

		if (!Wait->BegunPlayTime.IsSet())
		{
			if (PlayWorld && PlayWorld->HasBegunPlay())
			{
				Wait->BegunPlayTime = Now;
			}
			else if (!GEditor || !GEditor->IsPlaySessionInProgress())
			{
				Result.Fail(TEXT("PIE_START_FAILED"), TEXT("The play session request ended without starting a session."),
					FString::Printf(TEXT("Read log_get_recent with sinceSequence %lld for LogPlayLevel or Blueprint errors."), Wait->StartLogSequence));
				return;
			}
			else if (Now - Wait->RequestTime > Wait->TimeoutSeconds)
			{
				if (GEditor->IsPlaySessionRequestQueued())
				{
					GEditor->CancelRequestPlaySession();
				}
				Result.Fail(TEXT("TIMEOUT"), FString::Printf(TEXT("The play session did not begin play within %.0f seconds."), Wait->TimeoutSeconds),
					TEXT("Check pie_status; stop a half-started session with pie_stop."));
				return;
			}
			else
			{
				return;
			}
		}

		if (!PlayWorld)
		{
			Result.Fail(TEXT("PIE_ENDED"), TEXT("The play session ended during warmup."),
				FString::Printf(TEXT("Read log_get_recent with sinceSequence %lld."), Wait->StartLogSequence));
			return;
		}
		if (Now - Wait->BegunPlayTime.GetValue() < Wait->WarmupSeconds)
		{
			return;
		}

		// The viewport of a new window can differ from the window's client size by the window frame (2 pixels in height with UE 5.5 on
		// Windows), so the window is resized until the viewport has the requested size.
		if (Wait->RequestedViewportSize.X > 0 && Wait->ResizeAttempts < MaxWindowResizeAttempts)
		{
			const FViewport* PlayViewport = GEditor->GetPIEViewport();
			const FIntPoint ViewportSize = PlayViewport ? PlayViewport->GetSizeXY() : FIntPoint::ZeroValue;
			if (ViewportSize != Wait->RequestedViewportSize)
			{
				// After a resize, wait until the viewport has a new size, or for a while on a slow frame rate, before correcting again.
				if (Wait->ResizeAttempts > 0 && ViewportSize == Wait->SizeBeforeResize && Now - Wait->LastResizeTime < WindowResizeWaitSeconds)
				{
					return;
				}
				if (const TSharedPtr<SWindow> Window = FindPlayWindow(PlayViewport))
				{
					const FVector2D ClientSize = Window->GetClientSizeInScreen();
					Window->Resize(ClientSize + FVector2D(Wait->RequestedViewportSize - ViewportSize));
				}
				Wait->SizeBeforeResize = ViewportSize;
				Wait->LastResizeTime = Now;
				++Wait->ResizeAttempts;
				return;
			}
		}

		FAgentMcpPieStartResult StartResult;
		StartResult.PlaySession = UE::AgentMcp::Tools::MakePlaySessionState();
		StartResult.StartLogSequence = Wait->StartLogSequence;
		StartResult.StartupSeconds = Wait->BegunPlayTime.GetValue() - Wait->RequestTime;
		const FIntPoint ViewportSize = GetPlayViewportSize();
		StartResult.ViewportWidth = ViewportSize.X;
		StartResult.ViewportHeight = ViewportSize.Y;
		Result.CompleteWith(StartResult);
	});
}

UAgentMcpAsyncResult* UAgentMcpPieTools::Stop(float TimeoutSeconds)
{
	using namespace UE::AgentMcp::PieToolsPrivate;

	if (!GEditor)
	{
		UE::AgentMcp::RaiseToolError(TEXT("EDITOR_UNAVAILABLE"), TEXT("GEditor is not available."));
		return nullptr;
	}

	const bool bWasActive = GEditor->PlayWorld != nullptr || GEditor->IsPlaySessionInProgress();
	const double RequestTime = FPlatformTime::Seconds();
	const double Timeout = FMath::Clamp(TimeoutSeconds, 5.0f, 600.0f);

	if (bWasActive)
	{
		if (GEditor->IsPlaySessionRequestQueued())
		{
			// Requested but not started yet: drop the request.
			GEditor->CancelRequestPlaySession();
		}
		else
		{
			// The session ends on the next editor tick, outside this call; the result is sent after the shutdown has finished.
			GEditor->RequestEndPlayMap();
		}
	}

	return UAgentMcpAsyncResult::Create(static_cast<float>(Timeout), [bWasActive, RequestTime, Timeout](UAgentMcpAsyncResult& Result)
	{
		const bool bStillActive = GEditor && (GEditor->PlayWorld != nullptr || GEditor->IsPlaySessionInProgress());
		if (!bStillActive)
		{
			FAgentMcpPieStopResult StopResult;
			StopResult.bWasActive = bWasActive;
			StopResult.ShutdownSeconds = bWasActive ? FPlatformTime::Seconds() - RequestTime : 0.0;
			StopResult.PlaySession = UE::AgentMcp::Tools::MakePlaySessionState();
			StopResult.LatestLogSequence = GetLatestLogSequence();
			Result.CompleteWith(StopResult);
			return;
		}
		if (FPlatformTime::Seconds() - RequestTime > Timeout)
		{
			Result.Fail(TEXT("TIMEOUT"), FString::Printf(TEXT("The play session did not shut down within %.0f seconds."), Timeout),
				TEXT("Check pie_status and log_get_recent."));
		}
	});
}

FAgentMcpPieStatusResult UAgentMcpPieTools::GetStatus()
{
	FAgentMcpPieStatusResult Result;
	Result.PlaySession = UE::AgentMcp::Tools::MakePlaySessionState();
	Result.LatestLogSequence = UE::AgentMcp::PieToolsPrivate::GetLatestLogSequence();
	return Result;
}
