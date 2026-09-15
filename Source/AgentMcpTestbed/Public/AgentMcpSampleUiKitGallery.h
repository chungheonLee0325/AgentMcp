#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "AgentMcpSampleUiKitGallery.generated.h"

class UPanelWidget;

/**
 * Kit gallery of the UI sample, the Storybook of its theme. It fills its panels with every color token, text style, box style, bar style
 * and button style of the theme, in the designer and in the game, so a new or changed style appears without editing the gallery.
 * Components in their states are placed next to the panels in the Widget Blueprint.
 */
UCLASS(Abstract)
class AGENTMCPTESTBED_API UAgentMcpSampleUiKitGallery : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Receives a swatch for each color token; a Wrap Box suits it. */
	UPROPERTY(BlueprintReadOnly, Category = "Gallery", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> ColorList;

	/** Receives a line for each text style; a Wrap Box shows them in two columns. */
	UPROPERTY(BlueprintReadOnly, Category = "Gallery", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> TextList;

	/** Receives a sample for each box style; a Wrap Box suits it. */
	UPROPERTY(BlueprintReadOnly, Category = "Gallery", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> BoxList;

	/** Receives a bar for each bar style. */
	UPROPERTY(BlueprintReadOnly, Category = "Gallery", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> BarList;

	/** Receives the states and a live button for each button style. */
	UPROPERTY(BlueprintReadOnly, Category = "Gallery", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> ButtonList;

	/** Text that every text style shows. The default, "dungeon" in Korean and a time, shows the fallback font's Hangul next to Roboto digits. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gallery")
	FText SampleText = NSLOCTEXT("AgentMcpSample", "GallerySampleText", "\uB358\uC804 08:17");

protected:
	virtual void NativePreConstruct() override;

private:
	void FillPanels();
};
