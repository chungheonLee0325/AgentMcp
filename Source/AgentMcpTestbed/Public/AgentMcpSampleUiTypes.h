#pragma once

#include "CoreMinimal.h"

#include "AgentMcpSampleUiTypes.generated.h"

/** Rarity of a reward; it selects the reward slot colors. */
UENUM(BlueprintType)
enum class EAgentMcpSampleRarity : uint8
{
	Common,
	Uncommon,
	Rare,
	Epic,
	Legendary,
};

/** Outline shape of a reward icon. */
UENUM(BlueprintType)
enum class EAgentMcpSampleIconShape : uint8
{
	Square,
	Circle,
	Gem,
};

/** One reward of the dungeon result popup sample. */
USTRUCT(BlueprintType)
struct AGENTMCPTESTBED_API FAgentMcpSampleReward
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward")
	FText ItemName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward", meta = (ClampMin = "0"))
	int32 Count = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward")
	EAgentMcpSampleRarity Rarity = EAgentMcpSampleRarity::Common;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward")
	EAgentMcpSampleIconShape IconShape = EAgentMcpSampleIconShape::Square;
};
