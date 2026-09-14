#pragma once

#include "CoreMinimal.h"
#include "AgentMcpSampleUiTypes.h"
#include "Blueprint/UserWidget.h"

#include "AgentMcpSampleRewardSlot.generated.h"

class UBorder;
class UTextBlock;

/**
 * Reusable reward slot of the UI sample: an icon in the rarity color, the item name and the count. The Widget Blueprint provides the
 * layout and the brush shapes; this class applies the reward, also in the designer preview.
 */
UCLASS(Abstract)
class AGENTMCPTESTBED_API UAgentMcpSampleRewardSlot : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Slot frame; its outline takes the rarity color. */
	UPROPERTY(BlueprintReadOnly, Category = "Reward", meta = (BindWidget))
	TObjectPtr<UBorder> Frame;

	/** Icon; its fill and outline take the rarity color, its corners the icon shape. */
	UPROPERTY(BlueprintReadOnly, Category = "Reward", meta = (BindWidget))
	TObjectPtr<UBorder> Icon;

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
