#pragma once

#include "CoreMinimal.h"
#include "AgentMcpToolset.h"
#include "Blueprint/UserWidget.h"
#include "Templates/SubclassOf.h"

#include "AgentMcpSampleTools.generated.h"

USTRUCT(BlueprintType)
struct FAgentMcpSampleShowWidgetResult
{
	GENERATED_BODY()

	/** Class path of the shown widget. */
	UPROPERTY()
	FString WidgetClass;

	/** Widgets removed from the viewport first. */
	UPROPERTY()
	int32 RemovedWidgets = 0;
};

/** Helpers for the UI samples of the AgentMcp testbed. Not part of the AgentMcp plugin. */
UCLASS(meta = (McpToolset = "sample"))
class UAgentMcpSampleTools : public UAgentMcpToolset
{
	GENERATED_BODY()

public:
	/**
	 * Adds a widget to the viewport of the running play session, so that viewport_capture shows it.
	 * @param WidgetClass UserWidget class to show, such as the class of a Widget Blueprint (/Game/UI/WBP_Hud.WBP_Hud_C).
	 * @param ZOrder Widgets with a higher value are drawn on top.
	 * @param bRemoveOtherWidgets First remove the widgets on the viewport, such as the fixture HUD of the testbed game mode.
	 * @return The shown widget class and how many widgets were removed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Sample", meta = (AICallable, McpAccess = "Control", BlueprintInternalUseOnly = "true"))
	static FAgentMcpSampleShowWidgetResult ShowWidget(TSubclassOf<UUserWidget> WidgetClass, int32 ZOrder = 10, bool bRemoveOtherWidgets = true);
};
