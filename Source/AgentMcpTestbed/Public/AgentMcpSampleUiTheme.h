#pragma once

#include "CoreMinimal.h"
#include "AgentMcpSampleUiTypes.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"

#include "AgentMcpSampleUiTheme.generated.h"

class UFont;

/** How reward slots of one rarity look. */
USTRUCT(BlueprintType)
struct AGENTMCPTESTBED_API FAgentMcpSampleRarityStyle
{
	GENERATED_BODY()

	/** Outline of the slot frame and tint of the icon background. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rarity")
	FLinearColor Color = FLinearColor::White;

	/**
	 * Frame of the slot, usually a texture drawn as a 9-slice box (DrawAs Box with a Margin). While it has no image, the frame keeps its
	 * box style and only its outline takes Color.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rarity")
	FSlateBrush Frame;
};

/** A box that a Styled Border draws: fill, outline and corners made of the theme's tokens, and optionally the content padding. */
USTRUCT(BlueprintType)
struct AGENTMCPTESTBED_API FAgentMcpSampleBoxStyle
{
	GENERATED_BODY()

	/** Color token of the fill; None leaves the box without a fill. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box")
	FName Fill;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box", meta = (ClampMin = 0, ClampMax = 1))
	float FillOpacity = 1.0f;

	/** Color token of the outline; None draws no outline. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box")
	FName Line;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box", meta = (ClampMin = 0, ClampMax = 1))
	float LineOpacity = 1.0f;

	/** Outline width in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box", meta = (ClampMin = 0))
	float LineWidth = 1.0f;

	/** Radius token; None draws square corners. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box")
	FName Radius;

	/** Whether the style sets the padding of the border's content. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box", meta = (InlineEditConditionToggle))
	bool bOverride_Padding = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Box", meta = (EditCondition = "bOverride_Padding"))
	FMargin Padding;
};

/** Typography of a Styled Text: font, size and effects. The text color is a color token of the Styled Text or of the style. */
USTRUCT(BlueprintType)
struct AGENTMCPTESTBED_API FAgentMcpSampleTextStyle
{
	GENERATED_BODY()

	/** Font asset; the engine's Roboto when empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text")
	TSoftObjectPtr<UFont> Font;

	/** Typeface of the font, such as Regular or Bold. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text")
	FName Typeface = TEXT("Regular");

	/** Size, as in the Font of a Text Block. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text", meta = (ClampMin = 1))
	int32 Size = 16;

	/** Color token used when the Styled Text has no color of its own. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text")
	FName Color = TEXT("Text");

	/** Color token of the drop shadow; None draws no shadow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text")
	FName Shadow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text")
	FVector2D ShadowOffset = FVector2D(0.0, 2.0);

	/** Color token of the outline; None draws no outline. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text")
	FName Outline;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text", meta = (ClampMin = 0))
	int32 OutlineSize = 2;
};

/** A progress bar that a Styled Progress Bar draws: color tokens of the track and the fill, and a radius token. */
USTRUCT(BlueprintType)
struct AGENTMCPTESTBED_API FAgentMcpSampleBarStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bar")
	FName Track = TEXT("Track");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bar")
	FName Fill = TEXT("Accent");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bar")
	FName Radius = TEXT("Pill");
};

/** A button that a Styled Button draws: a box style for each state. */
USTRUCT(BlueprintType)
struct AGENTMCPTESTBED_API FAgentMcpSampleButtonStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Button")
	FName Normal;

	/** Box style while the cursor is over the button; None uses Normal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Button")
	FName Hovered;

	/** Box style while the button is pressed; None uses Hovered. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Button")
	FName Pressed;

	/** Box style of a disabled button; None uses Normal at half opacity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Button")
	FName Disabled;
};

/**
 * Style values of the UI sample in one data asset. Tokens are named colors and corner radii; styles are named boxes, texts, bars and
 * buttons made of tokens. Styled widgets (AgentMcpSampleStyledWidgets.h) take a style name and component code reads colors by token, so a
 * theme change restyles the sample without a build. The widgets use the theme of Project Settings > Game > Agent MCP Sample UI, or the
 * defaults of this class when none is set.
 */
UCLASS(BlueprintType)
class AGENTMCPTESTBED_API UAgentMcpSampleUiTheme : public UDataAsset
{
	GENERATED_BODY()

public:
	UAgentMcpSampleUiTheme();

	/** Colors by name, in linear space. Styles and code refer to these names; a name that is missing draws magenta. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tokens")
	TMap<FName, FLinearColor> Colors;

	/** Corner radii by name, in Slate units. A negative radius rounds the ends by half the height. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tokens")
	TMap<FName, float> Radii;

	/** Box styles of Styled Borders. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Styles")
	TMap<FName, FAgentMcpSampleBoxStyle> Boxes;

	/** Text styles of Styled Texts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Styles")
	TMap<FName, FAgentMcpSampleTextStyle> Texts;

	/** Bar styles of Styled Progress Bars. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Styles")
	TMap<FName, FAgentMcpSampleBarStyle> Bars;

	/** Button styles of Styled Buttons. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Styles")
	TMap<FName, FAgentMcpSampleButtonStyle> Buttons;

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

	/** The color of a token with its opacity multiplied by Opacity, or magenta when the theme has no such token. */
	FLinearColor GetColor(FName Token, float Opacity = 1.0f) const;

	/** The radius of a token: 0 for None or an unknown token, negative for rounded ends. */
	float GetRadius(FName Token) const;

	/** A rounded box brush of a box style. */
	FSlateBrush MakeBoxBrush(const FAgentMcpSampleBoxStyle& BoxStyle) const;

	/** The font of a text style, with its outline. */
	FSlateFontInfo MakeFont(const FAgentMcpSampleTextStyle& TextStyle) const;

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
