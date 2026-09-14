#pragma once

#include "CoreMinimal.h"
#include "AgentMcpSampleUiTypes.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "Styling/SlateBrush.h"

#include "AgentMcpSampleUiTheme.generated.h"

/** How reward slots of one rarity look. */
USTRUCT(BlueprintType)
struct AGENTMCPTESTBED_API FAgentMcpSampleRarityStyle
{
	GENERATED_BODY()

	/** Outline of the slot frame and tint of the icon background. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rarity")
	FLinearColor Color = FLinearColor::White;

	/**
	 * Frame of the slot, usually a texture drawn as a 9-slice box (DrawAs Box with a Margin). While it has no image, the frame keeps the
	 * brush of the Widget Blueprint and only its outline takes Color.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rarity")
	FSlateBrush Frame;
};

/**
 * Style values of the UI sample in one data asset: text and state colors and the look of each rarity. The widgets use the theme of
 * Project Settings > Game > Agent MCP Sample UI, or the defaults of this class when none is set, so a theme changes the sample
 * without a build.
 */
UCLASS(BlueprintType)
class AGENTMCPTESTBED_API UAgentMcpSampleUiTheme : public UDataAsset
{
	GENERATED_BODY()

public:
	UAgentMcpSampleUiTheme();

	/** Open objectives and highlights. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor Accent = FLinearColor(0.05f, 0.6f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor Text = FLinearColor::White;

	/** Completed or secondary text. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor MutedText = FLinearColor(0.55f, 0.64f, 0.75f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor Success = FLinearColor(0.22f, 0.8f, 0.3f);

	/** Warnings, such as the timer in its last minute. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor Danger = FLinearColor(1.0f, 0.22f, 0.18f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rarity")
	FAgentMcpSampleRarityStyle Common;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rarity")
	FAgentMcpSampleRarityStyle Uncommon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rarity")
	FAgentMcpSampleRarityStyle Rare;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rarity")
	FAgentMcpSampleRarityStyle Epic;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rarity")
	FAgentMcpSampleRarityStyle Legendary;

	const FAgentMcpSampleRarityStyle& GetRarityStyle(EAgentMcpSampleRarity Rarity) const;

	/** The theme of the project settings, or the class defaults when none is set or it cannot be loaded. */
	static const UAgentMcpSampleUiTheme& Get();
};

/** Project settings of the UI sample. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Agent MCP Sample UI"))
class AGENTMCPTESTBED_API UAgentMcpSampleUiSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	/** Theme of the sample widgets. Without one they use the defaults of the theme class. */
	UPROPERTY(Config, EditAnywhere, Category = "Theme")
	TSoftObjectPtr<UAgentMcpSampleUiTheme> Theme;
};
