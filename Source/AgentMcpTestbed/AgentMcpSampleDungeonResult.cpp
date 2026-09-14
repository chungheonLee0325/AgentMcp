#include "AgentMcpSampleDungeonResult.h"

#include "AgentMcpSampleRewardSlot.h"
#include "AgentMcpSampleStatTile.h"
#include "Components/Button.h"
#include "Components/DynamicEntryBox.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

namespace AgentMcpSampleDungeonResultPrivate
{
	/** Seconds from the first frame of the intro until every widget rests. */
	constexpr float IntroDuration = 2.8f;

	float Phase(float Time, float Start, float Duration)
	{
		return FMath::Clamp((Time - Start) / Duration, 0.0f, 1.0f);
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

void UAgentMcpSampleDungeonResult::PlayIntro()
{
	IntroTime = 0.0f;
	IdleTime = 0.0f;
	bPlaying = IntroSpeed > 0.0f;
	ApplyIntro(bPlaying ? 0.0f : AgentMcpSampleDungeonResultPrivate::IntroDuration);
}

void UAgentMcpSampleDungeonResult::FinishIntro()
{
	bPlaying = false;
	IntroTime = AgentMcpSampleDungeonResultPrivate::IntroDuration;
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
		bPlaying = IntroTime < IntroDuration;
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

	Pose(Backdrop, EaseOutCubic(Phase(Time, 0.0f, 0.3f)), FVector2D::ZeroVector, 1.0f);

	const float CardPhase = Phase(Time, 0.1f, 0.45f);
	Pose(Card, FMath::Min(1.0f, CardPhase * 2.5f), FVector2D(0.0f, 40.0f * (1.0f - EaseOutCubic(CardPhase))), FMath::Lerp(0.82f, 1.0f, EaseOutBack(CardPhase)));

	// The rank badge is stamped: large and tilted first, then it snaps to its size.
	const float RankPhase = Phase(Time, 0.65f, 0.3f);
	Pose(RankBadge, FMath::Min(1.0f, RankPhase * 3.0f), FVector2D::ZeroVector, FMath::Lerp(2.6f, 1.0f, EaseOutCubic(RankPhase)), FMath::Lerp(-24.0f, -8.0f, EaseOutCubic(RankPhase)));

	const float CountPhase = EaseOutCubic(Phase(Time, 0.7f, 0.8f));
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
			const float RewardPhase = Phase(Time, 1.0f + 0.12f * Index, 0.3f);
			Pose(Entries[Index], RewardPhase, FVector2D(0.0f, 24.0f * (1.0f - EaseOutCubic(RewardPhase))), FMath::Lerp(0.8f, 1.0f, EaseOutBack(RewardPhase)));
		}
	}

	// Experience: the bar fills to its end, wraps for the level-up and fills to the new value.
	const float ExpPhase = EaseOutCubic(Phase(Time, 1.4f, 1.2f));
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
		Pose(LevelUpBadge, 1.0f, FVector2D::ZeroVector, FMath::Lerp(1.4f, 1.0f, EaseOutCubic(Phase(Time, 2.1f, 0.3f))));
	}

	const float ButtonPhase = Phase(Time, 2.4f, 0.35f);
	Pose(ButtonRow, ButtonPhase, FVector2D(0.0f, 16.0f * (1.0f - EaseOutCubic(ButtonPhase))), 1.0f);
}

void UAgentMcpSampleDungeonResult::HandleConfirmClicked()
{
	RemoveFromParent();
}
