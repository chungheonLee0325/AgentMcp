#include "AgentMcpSampleStyledWidgets.h"

#include "AgentMcpSampleUiTheme.h"

#define LOCTEXT_NAMESPACE "AgentMcpSampleStyledWidgets"

void UAgentMcpSampleStyledBorder::SynchronizeProperties()
{
	const UAgentMcpSampleUiTheme& Theme = UAgentMcpSampleUiTheme::Get();
	if (const FAgentMcpSampleBoxStyle* BoxStyle = Theme.Boxes.Find(Style))
	{
		Background = Theme.MakeBoxBrush(*BoxStyle);
		if (BoxStyle->bOverride_Padding)
		{
			SetPadding(BoxStyle->Padding);
		}
	}
	Super::SynchronizeProperties();
}

void UAgentMcpSampleStyledText::SynchronizeProperties()
{
	const UAgentMcpSampleUiTheme& Theme = UAgentMcpSampleUiTheme::Get();
	if (const FAgentMcpSampleTextStyle* TextStyle = Theme.Texts.Find(Style))
	{
		SetFont(Theme.MakeFont(*TextStyle));
		SetColorAndOpacity(FSlateColor(Theme.GetColor(Color.IsNone() ? TextStyle->Color : Color)));
		SetShadowColorAndOpacity(TextStyle->Shadow.IsNone() ? FLinearColor::Transparent : Theme.GetColor(TextStyle->Shadow));
		SetShadowOffset(TextStyle->ShadowOffset);
	}
	Super::SynchronizeProperties();
}

void UAgentMcpSampleStyledProgressBar::SynchronizeProperties()
{
	const UAgentMcpSampleUiTheme& Theme = UAgentMcpSampleUiTheme::Get();
	if (const FAgentMcpSampleBarStyle* BarStyle = Theme.Bars.Find(Style))
	{
		FAgentMcpSampleBoxStyle TrackBox;
		TrackBox.Fill = BarStyle->Track;
		TrackBox.Radius = BarStyle->Radius;
		FAgentMcpSampleBoxStyle FillBox = TrackBox;
		FillBox.Fill = BarStyle->Fill;

		FProgressBarStyle Look = GetWidgetStyle();
		Look.BackgroundImage = Theme.MakeBoxBrush(TrackBox);
		Look.FillImage = Theme.MakeBoxBrush(FillBox);
		SetWidgetStyle(Look);
		// The fill brush carries the color, so the fill tint stays white.
		SetFillColorAndOpacity(FLinearColor::White);
	}
	Super::SynchronizeProperties();
}

void UAgentMcpSampleStyledButton::SynchronizeProperties()
{
	const UAgentMcpSampleUiTheme& Theme = UAgentMcpSampleUiTheme::Get();
	if (const FAgentMcpSampleButtonStyle* ButtonStyle = Theme.Buttons.Find(Style))
	{
		FButtonStyle Look = GetStyle();
		auto BrushOf = [&Theme](FName BoxName, const FSlateBrush& Fallback)
		{
			const FAgentMcpSampleBoxStyle* BoxStyle = Theme.Boxes.Find(BoxName);
			return BoxStyle ? Theme.MakeBoxBrush(*BoxStyle) : Fallback;
		};
		Look.Normal = BrushOf(ButtonStyle->Normal, Look.Normal);
		Look.Hovered = BrushOf(ButtonStyle->Hovered, Look.Normal);
		Look.Pressed = BrushOf(ButtonStyle->Pressed, Look.Hovered);
		if (const FAgentMcpSampleBoxStyle* DisabledBox = Theme.Boxes.Find(ButtonStyle->Disabled))
		{
			Look.Disabled = Theme.MakeBoxBrush(*DisabledBox);
		}
		else
		{
			Look.Disabled = Look.Normal;
			const FLinearColor FillColor = Look.Normal.TintColor.GetSpecifiedColor();
			const FLinearColor LineColor = Look.Normal.OutlineSettings.Color.GetSpecifiedColor();
			Look.Disabled.TintColor = FSlateColor(FillColor.CopyWithNewOpacity(FillColor.A * 0.5f));
			Look.Disabled.OutlineSettings.Color = FSlateColor(LineColor.CopyWithNewOpacity(LineColor.A * 0.5f));
		}
		SetStyle(Look);
	}
	Super::SynchronizeProperties();
}

#if WITH_EDITOR
const FText UAgentMcpSampleStyledBorder::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "Sample Styles");
}

const FText UAgentMcpSampleStyledText::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "Sample Styles");
}

const FText UAgentMcpSampleStyledProgressBar::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "Sample Styles");
}

const FText UAgentMcpSampleStyledButton::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "Sample Styles");
}
#endif

#undef LOCTEXT_NAMESPACE
