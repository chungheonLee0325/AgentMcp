#include "AgentMcpSampleUiTheme.h"

UAgentMcpSampleUiTheme::UAgentMcpSampleUiTheme()
{
	Common.Color = FLinearColor(0.62f, 0.68f, 0.75f);
	Uncommon.Color = FLinearColor(0.2f, 0.75f, 0.3f);
	Rare.Color = FLinearColor(0.08f, 0.4f, 1.0f);
	Epic.Color = FLinearColor(0.45f, 0.16f, 1.0f);
	Legendary.Color = FLinearColor(1.0f, 0.62f, 0.08f);
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
