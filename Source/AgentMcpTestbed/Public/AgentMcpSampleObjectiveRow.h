#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "AgentMcpSampleObjectiveRow.generated.h"

class UBorder;
class UTextBlock;

/**
 * Reusable objective row of the UI sample: a round check mark, the label and done/total. Open objectives use the accent color,
 * completed ones the success color of the UI theme. The Widget Blueprint provides the layout; this class applies the state, also in
 * the designer preview.
 */
UCLASS(Abstract)
class AGENTMCPTESTBED_API UAgentMcpSampleObjectiveRow : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Check mark: an outline in the accent color while open, filled when done. */
	UPROPERTY(BlueprintReadOnly, Category = "Objective", meta = (BindWidget))
	TObjectPtr<UBorder> Check;

	UPROPERTY(BlueprintReadOnly, Category = "Objective", meta = (BindWidget))
	TObjectPtr<UTextBlock> LabelText;

	UPROPERTY(BlueprintReadOnly, Category = "Objective", meta = (BindWidget))
	TObjectPtr<UTextBlock> CountText;

	/** Objective text. Instances placed in other Widget Blueprints set it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	FText Label;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective", meta = (ClampMin = "0"))
	int32 Done = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective", meta = (ClampMin = "1"))
	int32 Total = 1;

	/** Color token of the check outline and the count while the objective is open; None uses Accent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	FName OpenColor;

	UFUNCTION(BlueprintCallable, Category = "Objective")
	void SetProgress(int32 InDone, int32 InTotal);

protected:
	virtual void NativePreConstruct() override;

private:
	void ApplyProgress();
};
