#include "AgentMcpSampleObjectiveRow.h"

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
	const bool bComplete = Done >= Total;
	if (Check)
	{
		// Open objectives show an outline in the accent color, completed ones a filled green mark.
		FSlateBrush CheckBrush = Check->Background;
		CheckBrush.TintColor = FSlateColor(bComplete ? AgentMcpSampleStyle::Success : FLinearColor::Transparent);
		CheckBrush.OutlineSettings.Color = FSlateColor(bComplete ? AgentMcpSampleStyle::Success : AccentColor);
		CheckBrush.OutlineSettings.Width = bComplete ? 0.0f : 2.0f;
		Check->SetBrush(CheckBrush);
	}
	if (LabelText)
	{
		LabelText->SetText(Label);
		LabelText->SetColorAndOpacity(FSlateColor(bComplete ? AgentMcpSampleStyle::MutedText : AgentMcpSampleStyle::Text));
	}
	if (CountText)
	{
		CountText->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), Done, Total)));
		CountText->SetColorAndOpacity(FSlateColor(bComplete ? AgentMcpSampleStyle::Success : AccentColor));
	}
}
