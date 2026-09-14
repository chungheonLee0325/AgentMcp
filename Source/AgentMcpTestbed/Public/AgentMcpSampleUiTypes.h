#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"

#include "AgentMcpSampleUiTypes.generated.h"

class UTexture2D;

/** Rarity of an item; the UI theme maps it to colors and frames. */
UENUM(BlueprintType)
enum class EAgentMcpSampleRarity : uint8
{
	Common,
	Uncommon,
	Rare,
	Epic,
	Legendary,
};

/** Outline shape of a reward icon background. */
UENUM(BlueprintType)
enum class EAgentMcpSampleIconShape : uint8
{
	Square,
	Circle,
	Gem,
};

/** Row of the item table of the UI sample (/Game/Samples/DungeonUi/Data/DT_DungeonItems). */
USTRUCT(BlueprintType)
struct AGENTMCPTESTBED_API FAgentMcpSampleItemRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FText Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	EAgentMcpSampleRarity Rarity = EAgentMcpSampleRarity::Common;

	/** Icon texture. While it is empty, reward slots show only the icon background in FallbackShape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	TSoftObjectPtr<UTexture2D> Icon;

	/** Shape of the icon background. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	EAgentMcpSampleIconShape FallbackShape = EAgentMcpSampleIconShape::Square;
};

/** One reward of the dungeon result popup sample: an item of the item table and a count. */
USTRUCT(BlueprintType)
struct AGENTMCPTESTBED_API FAgentMcpSampleReward
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward", meta = (RowType = "/Script/AgentMcpTestbed.AgentMcpSampleItemRow"))
	FDataTableRowHandle Item;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward", meta = (ClampMin = "0"))
	int32 Count = 1;
};
