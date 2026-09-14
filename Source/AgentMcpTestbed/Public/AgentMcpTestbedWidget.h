#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "AgentMcpTestbedWidget.generated.h"

class UImage;
class UTextBlock;

/** Parent class of the Widget Blueprint fixtures, with a required and an optional BindWidget property. */
UCLASS(Abstract)
class AGENTMCPTESTBED_API UAgentMcpTestbedWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Required binding: Widget Blueprints of this class must contain a TextBlock named TitleText. */
	UPROPERTY(BlueprintReadOnly, Category = "Smoke", meta = (BindWidget))
	TObjectPtr<UTextBlock> TitleText;

	/** Optional binding. */
	UPROPERTY(BlueprintReadOnly, Category = "Smoke", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Icon;
};
