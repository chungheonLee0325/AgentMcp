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
	static const FName AccentToken(TEXT("Accent"));
	static const FName SuccessToken(TEXT("Success"));
	static const FName TextToken(TEXT("Text"));
	static const FName MutedTextToken(TEXT("MutedText"));

	const UAgentMcpSampleUiTheme& Theme = UAgentMcpSampleUiTheme::Get();
	const FLinearColor SuccessColor = Theme.GetColor(SuccessToken);
	const FLinearColor OpenTint = Theme.GetColor(OpenColor.IsNone() ? AccentToken : OpenColor);
	const bool bComplete = Done >= Total;
	if (Check)
	{
		// Open objectives show an outline in the open color, completed ones a filled mark in the success color.
		FSlateBrush CheckBrush = Check->Background;
		CheckBrush.TintColor = FSlateColor(bComplete ? SuccessColor : FLinearColor::Transparent);
		CheckBrush.OutlineSettings.Color = FSlateColor(bComplete ? SuccessColor : OpenTint);
		CheckBrush.OutlineSettings.Width = bComplete ? 0.0f : 2.0f;
		Check->SetBrush(CheckBrush);
	}
	if (LabelText)
	{
		LabelText->SetText(Label);
		LabelText->SetColorAndOpacity(FSlateColor(Theme.GetColor(bComplete ? MutedTextToken : TextToken)));
	}
	if (CountText)
	{
		CountText->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), Done, Total)));
		CountText->SetColorAndOpacity(FSlateColor(bComplete ? SuccessColor : OpenTint));
	}
}
