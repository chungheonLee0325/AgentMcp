#include "AgentMcpSampleRewardSlot.h"

#include "AgentMcpSampleUiTheme.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

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
	const UAgentMcpSampleUiTheme& Theme = UAgentMcpSampleUiTheme::Get();
	const FAgentMcpSampleItemRow* Item = Reward.Item.GetRow<FAgentMcpSampleItemRow>(TEXT("AgentMcpSampleRewardSlot"));
	const FAgentMcpSampleRarityStyle& Style = Theme.GetRarityStyle(Item ? Item->Rarity : EAgentMcpSampleRarity::Common);

	if (Frame)
	{
		if (Style.Frame.GetResourceObject())
		{
			Frame->SetBrush(Style.Frame);
		}
		else
		{
			// Without a frame image only the outline color comes from the theme; the rest of the brush stays as designed.
			FSlateBrush FrameBrush = Frame->Background;
			FrameBrush.OutlineSettings.Color = FSlateColor(Style.Color);
			Frame->SetBrush(FrameBrush);
		}
	}
	if (Icon)
	{
		FSlateBrush IconBrush = Icon->Background;
		IconBrush.TintColor = FSlateColor(Style.Color.CopyWithNewOpacity(0.3f));
		IconBrush.OutlineSettings.Color = FSlateColor(Style.Color);
		switch (Item ? Item->FallbackShape : EAgentMcpSampleIconShape::Square)
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
	if (IconImage)
	{
		// The item table refers to icons softly; load the icon here so that the designer preview shows it as well.
		if (UTexture2D* IconTexture = Item ? Item->Icon.LoadSynchronous() : nullptr)
		{
			IconImage->SetBrushFromTexture(IconTexture);
			IconImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			IconImage->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	if (NameText)
	{
		NameText->SetText(Item ? Item->Name : FText::GetEmpty());
	}
	if (CountText)
	{
		CountText->SetText(FText::Format(NSLOCTEXT("AgentMcpSample", "RewardCount", "x{0}"), FText::AsNumber(Reward.Count)));
	}
}
