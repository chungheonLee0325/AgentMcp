#include "AgentMcpSampleObjectiveRow.h"

#include "AgentMcpSampleUiTheme.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"

void UAgentMcpSampleObjectiveRow::SetProgress(int32 InDone, int32 InTotal)
{
	Done = FMath::Max(0, InDone);
	Total = FMath::Max(1, InTotal);
	ApplyProgress();
}

void UAgentMcpSampleObjectiveRow::NativePreConstruct()
{
	Super::NativePreConstruct();
	ApplyProgress();
}

void UAgentMcpSampleObjectiveRow::ApplyProgress()
{
	const UAgentMcpSampleUiTheme& Theme = UAgentMcpSampleUiTheme::Get();
	const FLinearColor OpenColor = AccentColor.A > 0.0f ? AccentColor : Theme.Accent;
	const bool bComplete = Done >= Total;
	if (Check)
	{
		// Open objectives show an outline in the accent color, completed ones a filled mark in the success color.
		FSlateBrush CheckBrush = Check->Background;
		CheckBrush.TintColor = FSlateColor(bComplete ? Theme.Success : FLinearColor::Transparent);
		CheckBrush.OutlineSettings.Color = FSlateColor(bComplete ? Theme.Success : OpenColor);
		CheckBrush.OutlineSettings.Width = bComplete ? 0.0f : 2.0f;
		Check->SetBrush(CheckBrush);
	}
	if (LabelText)
	{
		LabelText->SetText(Label);
		LabelText->SetColorAndOpacity(FSlateColor(bComplete ? Theme.MutedText : Theme.Text));
	}
	if (CountText)
	{
		CountText->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), Done, Total)));
		CountText->SetColorAndOpacity(FSlateColor(bComplete ? Theme.Success : OpenColor));
	}
}
