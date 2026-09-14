#pragma once

#include "CoreMinimal.h"
#include "AgentMcpSampleUiTypes.h"
#include "Blueprint/UserWidget.h"

#include "AgentMcpSampleDungeonResult.generated.h"

class UAgentMcpSampleStatTile;
class UButton;
class UDynamicEntryBox;
class UProgressBar;
class UTextBlock;

/**
 * C++ base of the dungeon result popup sample. A Widget Blueprint provides the layout and the style; the stat tiles and the reward
 * slots are reusable Widget Blueprints. This class fills in the results, creates one reward slot per reward and plays the intro:
 * the backdrop fades in, the card pops up, the rank badge is stamped, the rewards appear one after another and the experience bar
 * fills up to a level-up.
 */
UCLASS(Abstract)
class AGENTMCPTESTBED_API UAgentMcpSampleDungeonResult : public UUserWidget
{
	GENERATED_BODY()

public:
	/** The popup card; it pops up. */
	UPROPERTY(BlueprintReadOnly, Category = "Result", meta = (BindWidget))
	TObjectPtr<UWidget> Card;

	/** Clear time as mm:ss. */
	UPROPERTY(BlueprintReadOnly, Category = "Result", meta = (BindWidget))
	TObjectPtr<UAgentMcpSampleStatTile> TimeTile;

	UPROPERTY(BlueprintReadOnly, Category = "Result", meta = (BindWidget))
	TObjectPtr<UProgressBar> ExpBar;

	/** Defeated enemies, counted up. */
	UPROPERTY(BlueprintReadOnly, Category = "Result", meta = (BindWidgetOptional))
	TObjectPtr<UAgentMcpSampleStatTile> DefeatTile;

	/** Opened chests as done/total, counted up. */
	UPROPERTY(BlueprintReadOnly, Category = "Result", meta = (BindWidgetOptional))
	TObjectPtr<UAgentMcpSampleStatTile> ChestTile;

	/** Creates one entry, a reward slot Widget Blueprint, per reward; the entries appear one after another. */
	UPROPERTY(BlueprintReadOnly, Category = "Result", meta = (BindWidgetOptional))
	TObjectPtr<UDynamicEntryBox> RewardList;

	/** Dark backdrop behind the card; it fades in. */
	UPROPERTY(BlueprintReadOnly, Category = "Result", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> Backdrop;

	/** Rank badge; it is stamped onto the card. */
	UPROPERTY(BlueprintReadOnly, Category = "Result", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> RankBadge;

	/** Gained experience, counted up. */
	UPROPERTY(BlueprintReadOnly, Category = "Result", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ExpText;

	/** Level, raised when the experience bar wraps. */
	UPROPERTY(BlueprintReadOnly, Category = "Result", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LevelText;

	/** Shown when the experience bar wraps. */
	UPROPERTY(BlueprintReadOnly, Category = "Result", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> LevelUpBadge;

	/** Buttons at the bottom; they fade in last. */
	UPROPERTY(BlueprintReadOnly, Category = "Result", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> ButtonRow;

	/** Closes the popup. */
	UPROPERTY(BlueprintReadOnly, Category = "Result", meta = (BindWidgetOptional))
	TObjectPtr<UButton> ConfirmButton;

	/** Rewards, shown as reward slots in RewardList. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|Demo")
	TArray<FAgentMcpSampleReward> Rewards;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|Demo", meta = (ClampMin = "0"))
	float ClearSeconds = 497.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|Demo")
	int32 Defeated = 38;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|Demo")
	int32 ChestsOpened = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|Demo")
	int32 ChestCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|Demo")
	int32 ExpGained = 1820;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|Demo")
	int32 LevelBefore = 23;

	/** Levels gained by the reward: 0 or 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|Demo", meta = (ClampMin = "0", ClampMax = "1"))
	int32 LevelsGained = 1;

	/** Experience bar fill before the reward (0-1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|Demo", meta = (ClampMin = "0", ClampMax = "1"))
	float ExpBefore = 0.62f;

	/** Experience bar fill after the reward (0-1), in the new level when LevelsGained is 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|Demo", meta = (ClampMin = "0", ClampMax = "1"))
	float ExpAfter = 0.35f;

	/** Playback rate of the intro; 0 shows its end state at once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|Demo", meta = (ClampMin = "0"))
	float IntroSpeed = 1.0f;

	/** Plays the intro from the start. */
	UFUNCTION(BlueprintCallable, Category = "Result")
	void PlayIntro();

	/** Shows the end state of the intro at once. */
	UFUNCTION(BlueprintCallable, Category = "Result")
	void FinishIntro();

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	/** Creates one reward slot per reward in RewardList. */
	void RebuildRewards();

	/** Poses every animated widget for a point in the intro, in seconds. */
	void ApplyIntro(float Time);

	UFUNCTION()
	void HandleConfirmClicked();

	float IntroTime = 0.0f;
	float IdleTime = 0.0f;
	bool bPlaying = false;
};
