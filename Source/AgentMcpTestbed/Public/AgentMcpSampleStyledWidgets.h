#pragma once

#include "CoreMinimal.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

#include "AgentMcpSampleStyledWidgets.generated.h"

/*
 * Widgets that take their look from a named style of the UI theme (AgentMcpSampleUiTheme.h), much like an element with a CSS class.
 * They apply the style when their properties are synchronized, in the designer and in the game, so a theme change restyles every widget
 * that uses the style. Code that sets a brush or color afterwards, such as a rarity color, still wins. Without a style, or with a name
 * the theme does not have, a widget keeps its own properties.
 */

/** Border whose fill, outline, corner radius and padding come from a box style of the theme. */
UCLASS(meta = (DisplayName = "Styled Border"))
class AGENTMCPTESTBED_API UAgentMcpSampleStyledBorder : public UBorder
{
	GENERATED_BODY()

public:
	/** Name of a box style in Boxes of the theme. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FName Style;

	virtual void SynchronizeProperties() override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif
};

/** Text block whose font and effects come from a text style of the theme, and whose color comes from a color token. */
UCLASS(meta = (DisplayName = "Styled Text"))
class AGENTMCPTESTBED_API UAgentMcpSampleStyledText : public UTextBlock
{
	GENERATED_BODY()

public:
	/** Name of a text style in Texts of the theme. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FName Style;

	/** Color token of the text; None uses the color of the text style. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FName Color;

	virtual void SynchronizeProperties() override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif
};

/** Progress bar whose track and fill come from a bar style of the theme. */
UCLASS(meta = (DisplayName = "Styled Progress Bar"))
class AGENTMCPTESTBED_API UAgentMcpSampleStyledProgressBar : public UProgressBar
{
	GENERATED_BODY()

public:
	/** Name of a bar style in Bars of the theme. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FName Style;

	virtual void SynchronizeProperties() override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif
};

/** Button whose backgrounds for its states come from a button style of the theme. */
UCLASS(meta = (DisplayName = "Styled Button"))
class AGENTMCPTESTBED_API UAgentMcpSampleStyledButton : public UButton
{
	GENERATED_BODY()

public:
	/** Name of a button style in Buttons of the theme. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FName Style;

	virtual void SynchronizeProperties() override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif
};
