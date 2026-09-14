#include "AgentMcpSampleRewardSlot.h"

#include "AgentMcpSampleStyle.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"

void UAgentMcpSampleRewardSlot::SetReward(const FAgentMcpSampleReward& InReward)
{
	Reward = InReward;
	ApplyReward();
}

void UAgentMcpSampleRewardSlot::NativePreConstruct()
{
	Super::NativePreConstruct();
	ApplyReward();
}

void UAgentMcpSampleRewardSlot::ApplyReward()
{
	const FLinearColor RarityColor = AgentMcpSampleStyle::GetRarityColor(Reward.Rarity);

	// Only colors and the icon corners come from the data; the rest of each brush stays as designed in the Widget Blueprint.
	if (Frame)
	{
		FSlateBrush FrameBrush = Frame->Background;
		FrameBrush.OutlineSettings.Color = FSlateColor(RarityColor);
		Frame->SetBrush(FrameBrush);
	}
	if (Icon)
	{
		FSlateBrush IconBrush = Icon->Background;
		IconBrush.TintColor = FSlateColor(RarityColor.CopyWithNewOpacity(0.3f));
		IconBrush.OutlineSettings.Color = FSlateColor(RarityColor);
		switch (Reward.IconShape)
		{
		case EAgentMcpSampleIconShape::Circle:
			IconBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
			break;
		case EAgentMcpSampleIconShape::Gem:
			IconBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			IconBrush.OutlineSettings.CornerRadii = FVector4(4.0, 20.0, 4.0, 20.0);
			break;
		case EAgentMcpSampleIconShape::Square:
		default:
			IconBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			IconBrush.OutlineSettings.CornerRadii = FVector4(12.0, 12.0, 12.0, 12.0);
			break;
		}
		Icon->SetBrush(IconBrush);
	}
	if (NameText)
	{
		NameText->SetText(Reward.ItemName);
	}
	if (CountText)
	{
		CountText->SetText(FText::Format(NSLOCTEXT("AgentMcpSample", "RewardCount", "x{0}"), FText::AsNumber(Reward.Count)));
	}
}
