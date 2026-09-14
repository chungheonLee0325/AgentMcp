#pragma once

#include "CoreMinimal.h"
#include "AgentMcpSampleUiTypes.h"
#include "Blueprint/UserWidget.h"

#include "AgentMcpSampleRewardSlot.generated.h"

class UBorder;
class UImage;
class UTextBlock;

/**
 * Reusable reward slot of the UI sample: the item icon on a background in the rarity color, the item name and the count. The item
 * comes from the item table and the colors and frames from the UI theme; the Widget Blueprint provides the layout and the brush shapes.
 * This class applies the reward, also in the designer preview.
 */
UCLASS(Abstract)
class AGENTMCPTESTBED_API UAgentMcpSampleRewardSlot : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Slot frame; it takes the frame brush of the rarity from the theme, or the rarity color as its outline. */
	UPROPERTY(BlueprintReadOnly, Category = "Reward", meta = (BindWidget))
	TObjectPtr<UBorder> Frame;

	/** Icon background; its fill and outline take the rarity color and its corners the shape of the item. */
	UPROPERTY(BlueprintReadOnly, Category = "Reward", meta = (BindWidget))
	TObjectPtr<UBorder> Icon;

	/** Icon texture of the item; collapsed while the item has none. */
	UPROPERTY(BlueprintReadOnly, Category = "Reward", meta = (BindWidgetOptional))
	TObjectPtr<UImage> IconImage;

	UPROPERTY(BlueprintReadOnly, Category = "Reward", meta = (BindWidget))
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY(BlueprintReadOnly, Category = "Reward", meta = (BindWidget))
	TObjectPtr<UTextBlock> CountText;

	/** The reward to show. Instances placed in other Widget Blueprints can set it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward")
	FAgentMcpSampleReward Reward;

	/** Shows another reward. */
	UFUNCTION(BlueprintCallable, Category = "Reward")
	void SetReward(const FAgentMcpSampleReward& InReward);

protected:
	virtual void NativePreConstruct() override;

private:
	void ApplyReward();
};
