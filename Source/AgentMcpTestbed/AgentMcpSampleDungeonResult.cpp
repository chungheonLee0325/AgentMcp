#include "AgentMcpSampleDungeonResult.h"

#include "AgentMcpSampleRewardSlot.h"
#include "AgentMcpSampleStatTile.h"
#include "Components/Button.h"
#include "Components/DynamicEntryBox.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

namespace AgentMcpSampleDungeonResultPrivate
{
	float Phase(float Time, float Start, float Duration)
	{
		return FMath::Clamp((Time - Start) / FMath::Max(Duration, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
	}

	float EaseOutCubic(float X)
	{
		const float Inverse = 1.0f - X;
		return 1.0f - Inverse * Inverse * Inverse;
	}

	/** Overshoots a little before it settles, for pop-in motion. */
	float EaseOutBack(float X)
	{
		const float C1 = 1.70158f;
		const float C3 = C1 + 1.0f;
		const float Shifted = X - 1.0f;
		return 1.0f + C3 * Shifted * Shifted * Shifted + C1 * Shifted * Shifted;
	}

	void Pose(UWidget* Widget, float Opacity, const FVector2D& Translation, float Scale, float Angle = 0.0f)
	{
		if (Widget)
		{
			Widget->SetRenderOpacity(Opacity);
			Widget->SetRenderTransform(FWidgetTransform(Translation, FVector2D(Scale, Scale), FVector2D::ZeroVector, Angle));
		}
	}
}

void UAgentMcpSampleDungeonResult::NativePreConstruct()
{
	Super::NativePreConstruct();

	if (TimeTile)
	{
		const int32 Total = FMath::Max(0, FMath::RoundToInt(ClearSeconds));
		TimeTile->SetValue(FText::FromString(FString::Printf(TEXT("%02d:%02d"), Total / 60, Total % 60)));
	}
	// In the designer the DynamicEntryBox shows preview entries until rewards are set.
	if (!IsDesignTime() || Rewards.Num() > 0)
	{
		RebuildRewards();
	}
}

void UAgentMcpSampleDungeonResult::NativeConstruct()
{
	Super::NativeConstruct();

	if (ConfirmButton)
	{
		ConfirmButton->OnClicked.AddUniqueDynamic(this, &UAgentMcpSampleDungeonResult::HandleConfirmClicked);
	}
	PlayIntro();
}

void UAgentMcpSampleDungeonResult::NativeDestruct()
{
	if (ConfirmButton)
	{
		ConfirmButton->OnClicked.RemoveDynamic(this, &UAgentMcpSampleDungeonResult::HandleConfirmClicked);
	}
	Super::NativeDestruct();
}

void UAgentMcpSampleDungeonResult::RebuildRewards()
{
	if (!RewardList)
	{
		return;
	}
	RewardList->Reset();
	for (const FAgentMcpSampleReward& Reward : Rewards)
	{
		if (UAgentMcpSampleRewardSlot* RewardSlot = RewardList->CreateEntry<UAgentMcpSampleRewardSlot>())
		{
			RewardSlot->SetReward(Reward);
		}
	}
}

float UAgentMcpSampleDungeonResult::GetIntroDuration() const
{
	const int32 RewardCount = RewardList ? RewardList->GetAllEntries().Num() : 0;
	const float Ends[] = {
		Motion.BackdropDuration,
		Motion.CardDelay + Motion.CardDuration,
		Motion.RankDelay + Motion.RankDuration,
		Motion.CountDelay + Motion.CountDuration,
		Motion.RewardDelay + Motion.RewardInterval * FMath::Max(0, RewardCount - 1) + Motion.RewardDuration,
		Motion.ExpDelay + Motion.ExpDuration,
		Motion.LevelUpDelay + Motion.LevelUpDuration,
		Motion.ButtonDelay + Motion.ButtonDuration,
	};
	float Duration = 0.0f;
	for (const float End : Ends)
	{
		Duration = FMath::Max(Duration, End);
	}
	return Duration;
}

void UAgentMcpSampleDungeonResult::PlayIntro()
{
	IntroTime = 0.0f;
	IdleTime = 0.0f;
	bPlaying = IntroSpeed > 0.0f;
	ApplyIntro(bPlaying ? 0.0f : GetIntroDuration());
}

void UAgentMcpSampleDungeonResult::FinishIntro()
{
	bPlaying = false;
	IntroTime = GetIntroDuration();
	ApplyIntro(IntroTime);
}

void UAgentMcpSampleDungeonResult::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	using namespace AgentMcpSampleDungeonResultPrivate;

	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bPlaying)
	{
		IntroTime += InDeltaTime * IntroSpeed;
		ApplyIntro(IntroTime);
		bPlaying = IntroTime < GetIntroDuration();
		return;
	}

	// After the intro the level-up badge keeps breathing.
	if (LevelUpBadge && LevelUpBadge->GetVisibility() != ESlateVisibility::Collapsed)
	{
		IdleTime += InDeltaTime;
		Pose(LevelUpBadge, 1.0f, FVector2D::ZeroVector, 1.0f + 0.05f * FMath::Sin(IdleTime * 4.0f));
	}
}

void UAgentMcpSampleDungeonResult::ApplyIntro(float Time)
{
	using namespace AgentMcpSampleDungeonResultPrivate;

	Pose(Backdrop, EaseOutCubic(Phase(Time, 0.0f, Motion.BackdropDuration)), FVector2D::ZeroVector, 1.0f);

	const float CardPhase = Phase(Time, Motion.CardDelay, Motion.CardDuration);
	Pose(Card, FMath::Min(1.0f, CardPhase * 2.5f), FVector2D(0.0f, Motion.CardRise * (1.0f - EaseOutCubic(CardPhase))),
		FMath::Lerp(Motion.CardStartScale, 1.0f, EaseOutBack(CardPhase)));

	// The rank badge is stamped: large and tilted first, then it snaps to its size.
	const float RankPhase = EaseOutCubic(Phase(Time, Motion.RankDelay, Motion.RankDuration));
	Pose(RankBadge, FMath::Min(1.0f, Phase(Time, Motion.RankDelay, Motion.RankDuration) * 3.0f), FVector2D::ZeroVector,
		FMath::Lerp(Motion.RankStartScale, 1.0f, RankPhase), FMath::Lerp(Motion.RankStartAngle, Motion.RankEndAngle, RankPhase));

	const float CountPhase = EaseOutCubic(Phase(Time, Motion.CountDelay, Motion.CountDuration));
	if (DefeatTile)
	{
		DefeatTile->SetValue(FText::AsNumber(FMath::RoundToInt(Defeated * CountPhase)));
	}
	if (ChestTile)
	{
		ChestTile->SetValue(FText::FromString(FString::Printf(TEXT("%d/%d"), FMath::RoundToInt(ChestsOpened * CountPhase), ChestCount)));
	}

	if (RewardList)
	{
		const TArray<UUserWidget*>& Entries = RewardList->GetAllEntries();
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			const float RewardPhase = Phase(Time, Motion.RewardDelay + Motion.RewardInterval * Index, Motion.RewardDuration);
			Pose(Entries[Index], RewardPhase, FVector2D(0.0f, Motion.RewardRise * (1.0f - EaseOutCubic(RewardPhase))),
				FMath::Lerp(Motion.RewardStartScale, 1.0f, EaseOutBack(RewardPhase)));
		}
	}

	// Experience: the bar fills to its end, wraps for the level-up and fills to the new value.
	const float ExpPhase = EaseOutCubic(Phase(Time, Motion.ExpDelay, Motion.ExpDuration));
	if (ExpText)
	{
		ExpText->SetText(FText::Format(NSLOCTEXT("AgentMcpSample", "ExpGained", "+{0} EXP"), FText::AsNumber(FMath::RoundToInt(ExpGained * ExpPhase))));
	}
	const float Travel = LevelsGained > 0 ? (1.0f - ExpBefore) + ExpAfter : FMath::Max(0.0f, ExpAfter - ExpBefore);
	const float Filled = ExpBefore + Travel * ExpPhase;
	const bool bWrapped = LevelsGained > 0 && Filled >= 1.0f;
	if (ExpBar)
	{
		ExpBar->SetPercent(FMath::Clamp(bWrapped ? Filled - 1.0f : Filled, 0.0f, 1.0f));
	}
	if (LevelText)
	{
		LevelText->SetText(FText::Format(NSLOCTEXT("AgentMcpSample", "Level", "Lv.{0}"), FText::AsNumber(LevelBefore + (bWrapped ? LevelsGained : 0))));
	}
	if (LevelUpBadge)
	{
		LevelUpBadge->SetVisibility(bWrapped ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		Pose(LevelUpBadge, 1.0f, FVector2D::ZeroVector,
			FMath::Lerp(Motion.LevelUpStartScale, 1.0f, EaseOutCubic(Phase(Time, Motion.LevelUpDelay, Motion.LevelUpDuration))));
	}

	const float ButtonPhase = Phase(Time, Motion.ButtonDelay, Motion.ButtonDuration);
	Pose(ButtonRow, ButtonPhase, FVector2D(0.0f, Motion.ButtonRise * (1.0f - EaseOutCubic(ButtonPhase))), 1.0f);
}

void UAgentMcpSampleDungeonResult::HandleConfirmClicked()
{
	RemoveFromParent();
}
