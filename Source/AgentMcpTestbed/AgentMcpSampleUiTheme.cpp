#include "AgentMcpSampleUiTheme.h"

#include "Engine/Font.h"

namespace AgentMcpSampleUiThemePrivate
{
	FAgentMcpSampleBoxStyle MakeBox(FName Fill, float FillOpacity, FName Line, float LineOpacity, float LineWidth, FName Radius)
	{
		FAgentMcpSampleBoxStyle Style;
		Style.Fill = Fill;
		Style.FillOpacity = FillOpacity;
		Style.Line = Line;
		Style.LineOpacity = LineOpacity;
		Style.LineWidth = LineWidth;
		Style.Radius = Radius;
		return Style;
	}

	FAgentMcpSampleBoxStyle WithPadding(FAgentMcpSampleBoxStyle Style, const FMargin& Padding)
	{
		Style.bOverride_Padding = true;
		Style.Padding = Padding;
		return Style;
	}

	FAgentMcpSampleTextStyle MakeText(FName Typeface, int32 Size, FName Shadow = NAME_None, double ShadowY = 2.0)
	{
		FAgentMcpSampleTextStyle Style;
		Style.Typeface = Typeface;
		Style.Size = Size;
		Style.Shadow = Shadow;
		Style.ShadowOffset = FVector2D(0.0, ShadowY);
		return Style;
	}

	FAgentMcpSampleButtonStyle MakeButton(FName Normal, FName Hovered, FName Pressed)
	{
		FAgentMcpSampleButtonStyle Style;
		Style.Normal = Normal;
		Style.Hovered = Hovered;
		Style.Pressed = Pressed;
		return Style;
	}
}

UAgentMcpSampleUiTheme::UAgentMcpSampleUiTheme()
{
	using namespace AgentMcpSampleUiThemePrivate;

	// The defaults are the look that the sample's Widget Blueprints had, reduced to one value per role.
	Colors.Add(TEXT("Surface"), FLinearColor(0.006f, 0.016f, 0.035f));
	Colors.Add(TEXT("SurfaceOverlay"), FLinearColor(0.012f, 0.03f, 0.06f));
	Colors.Add(TEXT("SurfaceRaised"), FLinearColor(0.02f, 0.05f, 0.095f));
	Colors.Add(TEXT("SurfaceHover"), FLinearColor(0.04f, 0.09f, 0.16f));
	Colors.Add(TEXT("Scrim"), FLinearColor(0.0f, 0.005f, 0.015f));
	Colors.Add(TEXT("Line"), FLinearColor(1.0f, 1.0f, 1.0f, 0.1f));
	// The buttons had outline colors with a low opacity, but they drew them with the opacity of their fill (bUseBrushTransparency, the
	// default of UMG buttons), so the line on screen was opaque.
	Colors.Add(TEXT("LineStrong"), FLinearColor::White);
	Colors.Add(TEXT("Text"), FLinearColor::White);
	Colors.Add(TEXT("MutedText"), FLinearColor(0.55f, 0.64f, 0.75f));
	Colors.Add(TEXT("Accent"), FLinearColor(0.05f, 0.6f, 1.0f));
	Colors.Add(TEXT("AccentHover"), FLinearColor(0.2f, 0.72f, 1.0f));
	Colors.Add(TEXT("AccentPressed"), FLinearColor(0.03f, 0.4f, 0.75f));
	Colors.Add(TEXT("OnAccent"), FLinearColor(0.01f, 0.03f, 0.07f));
	Colors.Add(TEXT("Gold"), FLinearColor(1.0f, 0.62f, 0.08f));
	Colors.Add(TEXT("GoldDeep"), FLinearColor(0.25f, 0.1f, 0.0f));
	Colors.Add(TEXT("GoldLight"), FLinearColor(1.0f, 0.95f, 0.8f));
	Colors.Add(TEXT("OnGold"), FLinearColor(0.12f, 0.04f, 0.0f));
	Colors.Add(TEXT("Success"), FLinearColor(0.22f, 0.8f, 0.3f));
	Colors.Add(TEXT("Danger"), FLinearColor(1.0f, 0.12f, 0.08f));
	Colors.Add(TEXT("DangerDeep"), FLinearColor(0.3f, 0.015f, 0.01f));
	Colors.Add(TEXT("DangerTrack"), FLinearColor(0.08f, 0.01f, 0.01f));
	Colors.Add(TEXT("DangerSoft"), FLinearColor(1.0f, 0.3f, 0.2f));
	Colors.Add(TEXT("DangerLight"), FLinearColor(1.0f, 0.6f, 0.5f));
	Colors.Add(TEXT("Shadow"), FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
	Colors.Add(TEXT("Track"), FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));

	Radii.Add(TEXT("Small"), 8.0f);
	Radii.Add(TEXT("Medium"), 14.0f);
	Radii.Add(TEXT("Large"), 24.0f);
	Radii.Add(TEXT("Pill"), -1.0f);

	Boxes.Add(TEXT("Scrim"), MakeBox(TEXT("Scrim"), 0.66f, NAME_None, 1.0f, 0.0f, NAME_None));
	Boxes.Add(TEXT("Card"), WithPadding(MakeBox(TEXT("Surface"), 0.96f, TEXT("Accent"), 0.35f, 2.0f, TEXT("Large")), FMargin(32.0f, 28.0f)));
	Boxes.Add(TEXT("HudPanel"), WithPadding(MakeBox(TEXT("SurfaceOverlay"), 0.82f, TEXT("Line"), 1.0f, 1.0f, TEXT("Medium")), FMargin(20.0f, 16.0f)));
	Boxes.Add(TEXT("Tile"), WithPadding(MakeBox(TEXT("SurfaceRaised"), 0.95f, TEXT("Line"), 1.0f, 1.0f, TEXT("Medium")), FMargin(16.0f, 12.0f)));
	Boxes.Add(TEXT("SlotFill"), WithPadding(MakeBox(TEXT("SurfaceRaised"), 0.95f, NAME_None, 1.0f, 0.0f, TEXT("Medium")), FMargin(8.0f, 12.0f)));
	Boxes.Add(TEXT("SlotFrame"), MakeBox(NAME_None, 1.0f, TEXT("MutedText"), 1.0f, 2.0f, TEXT("Medium")));
	Boxes.Add(TEXT("IconBox"), WithPadding(MakeBox(TEXT("MutedText"), 0.3f, TEXT("MutedText"), 1.0f, 2.0f, TEXT("Medium")), FMargin(4.0f)));
	Boxes.Add(TEXT("Check"), MakeBox(NAME_None, 1.0f, TEXT("Accent"), 1.0f, 2.0f, TEXT("Pill")));
	Boxes.Add(TEXT("Divider"), MakeBox(TEXT("Line"), 1.0f, NAME_None, 1.0f, 0.0f, TEXT("Pill")));
	Boxes.Add(TEXT("BadgeAccent"), WithPadding(MakeBox(TEXT("Accent"), 1.0f, NAME_None, 1.0f, 0.0f, TEXT("Small")), FMargin(12.0f, 4.0f)));
	Boxes.Add(TEXT("BadgeGold"), WithPadding(MakeBox(TEXT("Gold"), 1.0f, NAME_None, 1.0f, 0.0f, TEXT("Pill")), FMargin(12.0f, 4.0f)));
	Boxes.Add(TEXT("BadgeDanger"), WithPadding(MakeBox(TEXT("DangerDeep"), 0.92f, NAME_None, 1.0f, 0.0f, TEXT("Small")), FMargin(8.0f, 4.0f)));
	Boxes.Add(TEXT("BarFrame"), WithPadding(MakeBox(TEXT("Shadow"), 1.0f, TEXT("DangerSoft"), 0.35f, 1.0f, TEXT("Small")), FMargin(4.0f)));
	Boxes.Add(TEXT("RankCircle"), MakeBox(TEXT("Gold"), 1.0f, TEXT("GoldLight"), 1.0f, 4.0f, TEXT("Pill")));
	Boxes.Add(TEXT("ButtonSecondary"), MakeBox(TEXT("SurfaceRaised"), 1.0f, TEXT("LineStrong"), 1.0f, 2.0f, TEXT("Medium")));
	Boxes.Add(TEXT("ButtonSecondaryHovered"), MakeBox(TEXT("SurfaceHover"), 1.0f, TEXT("LineStrong"), 1.0f, 2.0f, TEXT("Medium")));
	Boxes.Add(TEXT("ButtonSecondaryPressed"), MakeBox(TEXT("SurfaceOverlay"), 1.0f, TEXT("LineStrong"), 1.0f, 2.0f, TEXT("Medium")));
	Boxes.Add(TEXT("ButtonPrimary"), MakeBox(TEXT("Accent"), 1.0f, NAME_None, 1.0f, 0.0f, TEXT("Medium")));
	Boxes.Add(TEXT("ButtonPrimaryHovered"), MakeBox(TEXT("AccentHover"), 1.0f, NAME_None, 1.0f, 0.0f, TEXT("Medium")));
	Boxes.Add(TEXT("ButtonPrimaryPressed"), MakeBox(TEXT("AccentPressed"), 1.0f, NAME_None, 1.0f, 0.0f, TEXT("Medium")));

	const FName Regular(TEXT("Regular"));
	const FName Bold(TEXT("Bold"));
	const FName ShadowToken(TEXT("Shadow"));
	Texts.Add(TEXT("Display"), MakeText(Bold, 52));
	FAgentMcpSampleTextStyle Title = MakeText(Bold, 42, ShadowToken, 3.0);
	Title.Outline = TEXT("GoldDeep");
	Texts.Add(TEXT("Title"), Title);
	Texts.Add(TEXT("Value"), MakeText(Bold, 30));
	Texts.Add(TEXT("Heading"), MakeText(Bold, 26, ShadowToken));
	Texts.Add(TEXT("HeadingSmall"), MakeText(Bold, 20, ShadowToken));
	Texts.Add(TEXT("Subheading"), MakeText(Bold, 20));
	Texts.Add(TEXT("BodyStrong"), MakeText(Bold, 16));
	Texts.Add(TEXT("Body"), MakeText(Regular, 16));
	Texts.Add(TEXT("LabelStrong"), MakeText(Bold, 14));
	Texts.Add(TEXT("Label"), MakeText(Regular, 14));

	FAgentMcpSampleBarStyle DangerBar;
	DangerBar.Track = TEXT("DangerTrack");
	DangerBar.Fill = TEXT("Danger");
	Bars.Add(TEXT("Accent"), FAgentMcpSampleBarStyle());
	Bars.Add(TEXT("Danger"), DangerBar);

	Buttons.Add(TEXT("Primary"), MakeButton(TEXT("ButtonPrimary"), TEXT("ButtonPrimaryHovered"), TEXT("ButtonPrimaryPressed")));
	Buttons.Add(TEXT("Secondary"), MakeButton(TEXT("ButtonSecondary"), TEXT("ButtonSecondaryHovered"), TEXT("ButtonSecondaryPressed")));

	Common.Color = FLinearColor(0.62f, 0.68f, 0.75f);
	Uncommon.Color = FLinearColor(0.2f, 0.75f, 0.3f);
	Rare.Color = FLinearColor(0.08f, 0.4f, 1.0f);
	Epic.Color = FLinearColor(0.45f, 0.16f, 1.0f);
	Legendary.Color = FLinearColor(1.0f, 0.62f, 0.08f);
}

FLinearColor UAgentMcpSampleUiTheme::GetColor(FName Token, float Opacity) const
{
	if (const FLinearColor* Color = Colors.Find(Token))
	{
		return Color->CopyWithNewOpacity(Color->A * Opacity);
	}
	// Magenta makes a misspelled or missing token visible on screen.
	return FLinearColor(1.0f, 0.0f, 1.0f, Opacity);
}

float UAgentMcpSampleUiTheme::GetRadius(FName Token) const
{
	const float* Radius = Radii.Find(Token);
	return Radius ? *Radius : 0.0f;
}

FSlateBrush UAgentMcpSampleUiTheme::MakeBoxBrush(const FAgentMcpSampleBoxStyle& BoxStyle) const
{
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.TintColor = FSlateColor(BoxStyle.Fill.IsNone() ? FLinearColor::Transparent : GetColor(BoxStyle.Fill, BoxStyle.FillOpacity));
	Brush.OutlineSettings.Color = FSlateColor(BoxStyle.Line.IsNone() ? FLinearColor::Transparent : GetColor(BoxStyle.Line, BoxStyle.LineOpacity));
	Brush.OutlineSettings.Width = BoxStyle.Line.IsNone() ? 0.0f : BoxStyle.LineWidth;
	const float Radius = GetRadius(BoxStyle.Radius);
	if (Radius < 0.0f)
	{
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
	}
	else
	{
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.OutlineSettings.CornerRadii = FVector4(Radius, Radius, Radius, Radius);
	}
	return Brush;
}

FSlateFontInfo UAgentMcpSampleUiTheme::MakeFont(const FAgentMcpSampleTextStyle& TextStyle) const
{
	static const FSoftObjectPath Roboto(TEXT("/Engine/EngineFonts/Roboto.Roboto"));
	const UObject* FontObject = TextStyle.Font.IsNull() ? Roboto.TryLoad() : TextStyle.Font.LoadSynchronous();
	FSlateFontInfo FontInfo(FontObject, static_cast<float>(TextStyle.Size), TextStyle.Typeface);
	if (!TextStyle.Outline.IsNone() && TextStyle.OutlineSize > 0)
	{
		FontInfo.OutlineSettings.OutlineSize = TextStyle.OutlineSize;
		FontInfo.OutlineSettings.OutlineColor = GetColor(TextStyle.Outline);
	}
	return FontInfo;
}

const FAgentMcpSampleRarityStyle& UAgentMcpSampleUiTheme::GetRarityStyle(EAgentMcpSampleRarity Rarity) const
{
	switch (Rarity)
	{
	case EAgentMcpSampleRarity::Uncommon:
		return Uncommon;
	case EAgentMcpSampleRarity::Rare:
		return Rare;
	case EAgentMcpSampleRarity::Epic:
		return Epic;
	case EAgentMcpSampleRarity::Legendary:
		return Legendary;
	case EAgentMcpSampleRarity::Common:
	default:
		return Common;
	}
}

const UAgentMcpSampleUiTheme& UAgentMcpSampleUiTheme::Get()
{
	if (const UAgentMcpSampleUiTheme* Theme = GetDefault<UAgentMcpSampleUiSettings>()->Theme.LoadSynchronous())
	{
		return *Theme;
	}
	return *GetDefault<UAgentMcpSampleUiTheme>();
}
