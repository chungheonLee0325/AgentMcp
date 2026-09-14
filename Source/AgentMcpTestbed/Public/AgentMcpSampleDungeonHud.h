#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "AgentMcpSampleDungeonHud.generated.h"

class UAgentMcpSampleObjectiveRow;
class UProgressBar;
class UTextBlock;

/**
 * C++ base of the dungeon progress HUD sample. A Widget Blueprint provides the layout and the style; the objective rows are a
 * reusable Widget Blueprint. This class shows demo values: it counts the remaining time down, advances the progress, fills in the
 * objectives and shows the boss panel near the end of the dungeon.
 */
UCLASS(Abstract)
class AGENTMCPTESTBED_API UAgentMcpSampleDungeonHud : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Remaining time as mm:ss. */
	UPROPERTY(BlueprintReadOnly, Category = "Dungeon", meta = (BindWidget))
	TObjectPtr<UTextBlock> TimerText;

	/** Progress through the dungeon. */
	UPROPERTY(BlueprintReadOnly, Category = "Dungeon", meta = (BindWidget))
	TObjectPtr<UProgressBar> ProgressBar;

	/** Progress in percent. */
	UPROPERTY(BlueprintReadOnly, Category = "Dungeon", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ProgressText;

	UPROPERTY(BlueprintReadOnly, Category = "Dungeon", meta = (BindWidgetOptional))
	TObjectPtr<UAgentMcpSampleObjectiveRow> BossObjective;

	UPROPERTY(BlueprintReadOnly, Category = "Dungeon", meta = (BindWidgetOptional))
	TObjectPtr<UAgentMcpSampleObjectiveRow> ChestObjective;

	UPROPERTY(BlueprintReadOnly, Category = "Dungeon", meta = (BindWidgetOptional))
	TObjectPtr<UAgentMcpSampleObjectiveRow> RescueObjective;

	/** Completed objectives as done/total. */
	UPROPERTY(BlueprintReadOnly, Category = "Dungeon", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CompletedObjectivesText;

	/** Shown once the progress reaches BossProgress. */
	UPROPERTY(BlueprintReadOnly, Category = "Dungeon", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> BossPanel;

	UPROPERTY(BlueprintReadOnly, Category = "Dungeon", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> BossHealthBar;

	/** Seconds left when the HUD is constructed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon|Demo", meta = (ClampMin = "0"))
	float TimeLeftSeconds = 462.0f;

	/** Progress when the HUD is constructed (0-1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon|Demo", meta = (ClampMin = "0", ClampMax = "1"))
	float Progress = 0.68f;

	/** Progress gained per second while the HUD is shown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon|Demo", meta = (ClampMin = "0"))
	float ProgressPerSecond = 0.01f;

	/** Progress at which the boss panel appears (0-1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon|Demo", meta = (ClampMin = "0", ClampMax = "1"))
	float BossProgress = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon|Demo")
	int32 BossesDefeated = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon|Demo")
	int32 BossCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon|Demo")
	int32 ChestsOpened = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon|Demo")
	int32 ChestCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon|Demo")
	int32 Rescued = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon|Demo")
	int32 RescueCount = 1;

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	/** Writes the objective counts into the objective rows. */
	void RefreshObjectives();

	/** Writes the remaining time and the progress into the bound widgets. */
	void RefreshProgress();

	/** Seconds since construction, for the pulse of the timer in the last minute. */
	float RunningTime = 0.0f;
};
