#pragma once

#include "CoreMinimal.h"
#include "AgentMcpSampleUiTypes.h"

/**
 * Color tokens of the UI sample, in linear color. The C++ widget classes use them for colors that depend on data; the Widget
 * Blueprints use the same values for their static brushes.
 */
namespace AgentMcpSampleStyle
{
	inline const FLinearColor Accent(0.05f, 0.6f, 1.0f);
	inline const FLinearColor Gold(1.0f, 0.62f, 0.08f);
	inline const FLinearColor Text(1.0f, 1.0f, 1.0f);
	inline const FLinearColor MutedText(0.55f, 0.64f, 0.75f);
	inline const FLinearColor Success(0.22f, 0.8f, 0.3f);
	inline const FLinearColor Danger(1.0f, 0.22f, 0.18f);

	inline FLinearColor GetRarityColor(EAgentMcpSampleRarity Rarity)
	{
		switch (Rarity)
		{
		case EAgentMcpSampleRarity::Uncommon:
			return FLinearColor(0.2f, 0.75f, 0.3f);
		case EAgentMcpSampleRarity::Rare:
			return FLinearColor(0.08f, 0.4f, 1.0f);
		case EAgentMcpSampleRarity::Epic:
			return FLinearColor(0.45f, 0.16f, 1.0f);
		case EAgentMcpSampleRarity::Legendary:
			return Gold;
		case EAgentMcpSampleRarity::Common:
		default:
			return FLinearColor(0.62f, 0.68f, 0.75f);
		}
	}
}
