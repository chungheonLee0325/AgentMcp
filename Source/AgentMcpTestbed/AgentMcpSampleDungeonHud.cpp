#include "AgentMcpSampleDungeonHud.h"

#include "AgentMcpSampleObjectiveRow.h"
#include "AgentMcpSampleUiTheme.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

namespace AgentMcpSampleDungeonHudPrivate
{
	FText FormatClock(float Seconds)
	{
		const int32 Total = FMath::Max(0, FMath::CeilToInt(Seconds));
		return FText::FromString(FString::Printf(TEXT("%02d:%02d"), Total / 60, Total % 60));
	}

	FText FormatCount(int32 Done, int32 Total)
	{
		return FText::FromString(FString::Printf(TEXT("%d/%d"), Done, Total));
	}
}

void UAgentMcpSampleDungeonHud::NativePreConstruct()
{
	Super::NativePreConstruct();
	RefreshObjectives();
}

void UAgentMcpSampleDungeonHud::NativeConstruct()
{
	Super::NativeConstruct();
	RunningTime = 0.0f;
	RefreshObjectives();
	RefreshProgress();
}

void UAgentMcpSampleDungeonHud::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	RunningTime += InDeltaTime;
	TimeLeftSeconds = FMath::Max(0.0f, TimeLeftSeconds - InDeltaTime);
	Progress = FMath::Min(1.0f, Progress + ProgressPerSecond * InDeltaTime);
	RefreshProgress();
}

void UAgentMcpSampleDungeonHud::RefreshObjectives()
{
	using namespace AgentMcpSampleDungeonHudPrivate;

	struct FObjective
	{
		UAgentMcpSampleObjectiveRow* Row;
		int32 Done;
		int32 Total;
	};
	const FObjective Objectives[] = {
		{ BossObjective.Get(), BossesDefeated, BossCount },
		{ ChestObjective.Get(), ChestsOpened, ChestCount },
		{ RescueObjective.Get(), Rescued, RescueCount },
	};

	int32 Completed = 0;
	for (const FObjective& Objective : Objectives)
	{
		if (Objective.Row)
		{
			Objective.Row->SetProgress(Objective.Done, Objective.Total);
		}
		Completed += Objective.Done >= Objective.Total ? 1 : 0;
	}
	if (CompletedObjectivesText)
	{
		CompletedObjectivesText->SetText(FormatCount(Completed, UE_ARRAY_COUNT(Objectives)));
	}
}

void UAgentMcpSampleDungeonHud::RefreshProgress()
{
	using namespace AgentMcpSampleDungeonHudPrivate;

	if (TimerText)
	{
		TimerText->SetText(FormatClock(TimeLeftSeconds));
		if (TimeLeftSeconds < 60.0f)
		{
			// In the last minute the timer pulses between the text and danger colors of the theme.
			const float Pulse = 0.5f + 0.5f * FMath::Sin(RunningTime * 6.0f);
			static const FName TextToken(TEXT("Text"));
			static const FName DangerToken(TEXT("Danger"));
			const UAgentMcpSampleUiTheme& Theme = UAgentMcpSampleUiTheme::Get();
			TimerText->SetColorAndOpacity(FSlateColor(FMath::Lerp(Theme.GetColor(TextToken), Theme.GetColor(DangerToken), Pulse)));
		}
	}
	if (ProgressBar)
	{
		ProgressBar->SetPercent(Progress);
	}
	if (ProgressText)
	{
		ProgressText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Progress * 100.0f))));
	}

	const bool bBossVisible = Progress >= BossProgress;
	if (BossPanel)
	{
		BossPanel->SetVisibility(bBossVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (BossHealthBar && bBossVisible)
	{
		// The boss loses health while the progress approaches the end.
		const float BossPhase = BossProgress < 1.0f ? (Progress - BossProgress) / (1.0f - BossProgress) : 1.0f;
		BossHealthBar->SetPercent(FMath::Clamp(1.0f - 0.75f * BossPhase, 0.0f, 1.0f));
	}
}
