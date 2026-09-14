#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "AgentMcpSampleStatTile.generated.h"

class UTextBlock;

/** Reusable stat tile of the UI sample: a label above a large value. The Widget Blueprint provides the layout and the style. */
UCLASS(Abstract)
class AGENTMCPTESTBED_API UAgentMcpSampleStatTile : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Stat", meta = (BindWidget))
	TObjectPtr<UTextBlock> LabelText;

	UPROPERTY(BlueprintReadOnly, Category = "Stat", meta = (BindWidget))
	TObjectPtr<UTextBlock> ValueText;

	/** Caption above the value. Instances placed in other Widget Blueprints set it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat")
	FText Label;

	/** Value shown until the owner sets another one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat")
	FText Value;

	UFUNCTION(BlueprintCallable, Category = "Stat")
	void SetValue(const FText& InValue);

protected:
	virtual void NativePreConstruct() override;
};
