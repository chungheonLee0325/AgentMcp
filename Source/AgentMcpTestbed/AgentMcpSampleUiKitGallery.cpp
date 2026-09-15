#include "AgentMcpSampleUiKitGallery.h"

#include "AgentMcpSampleStyledWidgets.h"
#include "AgentMcpSampleUiTheme.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBoxSlot.h"

namespace AgentMcpSampleUiKitGalleryPrivate
{
	const FName LabelStyle(TEXT("Label"));
	const FName LabelStrongStyle(TEXT("LabelStrong"));
	const FName TextToken(TEXT("Text"));
	const FName MutedTextToken(TEXT("MutedText"));
	const FName OnAccentToken(TEXT("OnAccent"));

	UAgentMcpSampleStyledText* MakeText(UWidgetTree& Tree, FName Style, FName Color, const FText& Content)
	{
		UAgentMcpSampleStyledText* Text = Tree.ConstructWidget<UAgentMcpSampleStyledText>();
		Text->Style = Style;
		Text->Color = Color;
		Text->SetText(Content);
		return Text;
	}

	/** A size box around Content; a width or height of 0 leaves that dimension to the content. */
	USizeBox* MakeSized(UWidgetTree& Tree, UWidget* Content, float Width, float Height)
	{
		USizeBox* Size = Tree.ConstructWidget<USizeBox>();
		if (Width > 0.0f)
		{
			Size->SetWidthOverride(Width);
		}
		if (Height > 0.0f)
		{
			Size->SetHeightOverride(Height);
		}
		Size->AddChild(Content);
		return Size;
	}

	/** Adds an entry to a list panel with a gap after it, whatever panel the Widget Blueprint uses. */
	void AddEntry(UPanelWidget& List, UWidget* Entry, float Gap)
	{
		UPanelSlot* Slot = List.AddChild(Entry);
		if (UVerticalBoxSlot* VerticalSlot = Cast<UVerticalBoxSlot>(Slot))
		{
			VerticalSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, Gap));
		}
		else if (UHorizontalBoxSlot* HorizontalSlot = Cast<UHorizontalBoxSlot>(Slot))
		{
			HorizontalSlot->SetPadding(FMargin(0.0f, 0.0f, Gap, 0.0f));
		}
		else if (UWrapBoxSlot* WrapSlot = Cast<UWrapBoxSlot>(Slot))
		{
			WrapSlot->SetPadding(FMargin(0.0f, 0.0f, Gap, Gap));
		}
	}

	/** A row that starts with a name column, for the samples of bar and button styles. */
	UHorizontalBox* MakeNamedRow(UWidgetTree& Tree, FName Name)
	{
		UHorizontalBox* Row = Tree.ConstructWidget<UHorizontalBox>();
		Row->AddChildToHorizontalBox(MakeSized(Tree, MakeText(Tree, LabelStyle, MutedTextToken, FText::FromName(Name)), 110.0f, 0.0f))
			->SetVerticalAlignment(VAlign_Center);
		return Row;
	}

	FText ColorText(const FLinearColor& Color)
	{
		const FString Hex = Color.ToFColor(true).ToHex().Left(6);
		return FText::FromString(Color.A < 1.0f ? FString::Printf(TEXT("#%s %d%%"), *Hex, FMath::RoundToInt(Color.A * 100.0f)) : TEXT("#") + Hex);
	}

	/** The text color token that reads on a swatch of Color, which covers the dark card with its opacity. */
	FName SwatchTextToken(const FLinearColor& Color)
	{
		return Color.GetLuminance() * Color.A > 0.35f ? OnAccentToken : TextToken;
	}
}

void UAgentMcpSampleUiKitGallery::NativePreConstruct()
{
	Super::NativePreConstruct();
	FillPanels();
}

void UAgentMcpSampleUiKitGallery::FillPanels()
{
	using namespace AgentMcpSampleUiKitGalleryPrivate;

	if (!WidgetTree)
	{
		return;
	}
	UWidgetTree& Tree = *WidgetTree;
	const UAgentMcpSampleUiTheme& Theme = UAgentMcpSampleUiTheme::Get();

	if (ColorList)
	{
		ColorList->ClearChildren();
		for (const TPair<FName, FLinearColor>& Token : Theme.Colors)
		{
			FAgentMcpSampleBoxStyle SwatchStyle;
			SwatchStyle.Fill = Token.Key;
			SwatchStyle.Line = TEXT("Line");
			SwatchStyle.Radius = TEXT("Small");
			UBorder* Swatch = Tree.ConstructWidget<UBorder>();
			Swatch->SetBrush(Theme.MakeBoxBrush(SwatchStyle));
			Swatch->SetPadding(FMargin(8.0f, 0.0f));
			Swatch->SetVerticalAlignment(VAlign_Center);
			Swatch->AddChild(MakeText(Tree, LabelStyle, SwatchTextToken(Token.Value), ColorText(Token.Value)));

			UVerticalBox* Entry = Tree.ConstructWidget<UVerticalBox>();
			Entry->AddChildToVerticalBox(MakeSized(Tree, Swatch, 0.0f, 32.0f));
			Entry->AddChildToVerticalBox(MakeText(Tree, LabelStrongStyle, NAME_None, FText::FromName(Token.Key)));
			AddEntry(*ColorList, MakeSized(Tree, Entry, 164.0f, 0.0f), 8.0f);
		}
	}

	if (TextList)
	{
		TextList->ClearChildren();
		for (const TPair<FName, FAgentMcpSampleTextStyle>& TextStyle : Theme.Texts)
		{
			UHorizontalBox* Entry = Tree.ConstructWidget<UHorizontalBox>();
			UAgentMcpSampleStyledText* Name = MakeText(Tree, LabelStyle, MutedTextToken, FText::FromString(FString::Printf(
				TEXT("%s  %s %d"), *TextStyle.Key.ToString(), *TextStyle.Value.Typeface.ToString(), TextStyle.Value.Size)));
			Entry->AddChildToHorizontalBox(MakeSized(Tree, Name, 230.0f, 0.0f))->SetVerticalAlignment(VAlign_Center);
			Entry->AddChildToHorizontalBox(MakeText(Tree, TextStyle.Key, NAME_None, SampleText))->SetVerticalAlignment(VAlign_Center);
			// A fixed width lets a Wrap Box put the lines into two columns.
			AddEntry(*TextList, MakeSized(Tree, Entry, 596.0f, 0.0f), 6.0f);
		}
	}

	if (BoxList)
	{
		BoxList->ClearChildren();
		// The boxes of button states appear with the buttons.
		TSet<FName> ButtonBoxes;
		for (const TPair<FName, FAgentMcpSampleButtonStyle>& ButtonStyle : Theme.Buttons)
		{
			ButtonBoxes.Add(ButtonStyle.Value.Normal);
			ButtonBoxes.Add(ButtonStyle.Value.Hovered);
			ButtonBoxes.Add(ButtonStyle.Value.Pressed);
			ButtonBoxes.Add(ButtonStyle.Value.Disabled);
		}
		for (const TPair<FName, FAgentMcpSampleBoxStyle>& BoxStyle : Theme.Boxes)
		{
			if (ButtonBoxes.Contains(BoxStyle.Key))
			{
				continue;
			}
			UAgentMcpSampleStyledBorder* Box = Tree.ConstructWidget<UAgentMcpSampleStyledBorder>();
			Box->Style = BoxStyle.Key;
			UVerticalBox* Entry = Tree.ConstructWidget<UVerticalBox>();
			Entry->AddChildToVerticalBox(MakeSized(Tree, Box, 0.0f, 36.0f));
			Entry->AddChildToVerticalBox(MakeText(Tree, LabelStyle, MutedTextToken, FText::FromName(BoxStyle.Key)));
			AddEntry(*BoxList, MakeSized(Tree, Entry, 128.0f, 0.0f), 8.0f);
		}
	}

	if (BarList)
	{
		BarList->ClearChildren();
		for (const TPair<FName, FAgentMcpSampleBarStyle>& BarStyle : Theme.Bars)
		{
			UAgentMcpSampleStyledProgressBar* Bar = Tree.ConstructWidget<UAgentMcpSampleStyledProgressBar>();
			Bar->Style = BarStyle.Key;
			Bar->SetPercent(0.65f);
			UHorizontalBox* Entry = MakeNamedRow(Tree, BarStyle.Key);
			Entry->AddChildToHorizontalBox(MakeSized(Tree, Bar, 240.0f, 12.0f))->SetVerticalAlignment(VAlign_Center);
			AddEntry(*BarList, Entry, 12.0f);
		}
	}

	if (ButtonList)
	{
		ButtonList->ClearChildren();
		for (const TPair<FName, FAgentMcpSampleButtonStyle>& ButtonStyle : Theme.Buttons)
		{
			// The normal, hovered and pressed boxes, then a live button.
			UHorizontalBox* Entry = MakeNamedRow(Tree, ButtonStyle.Key);
			const FName StateBoxes[] = {ButtonStyle.Value.Normal, ButtonStyle.Value.Hovered, ButtonStyle.Value.Pressed};
			for (const FName StateBox : StateBoxes)
			{
				UAgentMcpSampleStyledBorder* State = Tree.ConstructWidget<UAgentMcpSampleStyledBorder>();
				State->Style = StateBox;
				Entry->AddChildToHorizontalBox(MakeSized(Tree, State, 72.0f, 36.0f))->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
			}
			UAgentMcpSampleStyledButton* Button = Tree.ConstructWidget<UAgentMcpSampleStyledButton>();
			Button->Style = ButtonStyle.Key;
			Entry->AddChildToHorizontalBox(MakeSized(Tree, Button, 100.0f, 36.0f));
			AddEntry(*ButtonList, Entry, 12.0f);
		}
	}
}
