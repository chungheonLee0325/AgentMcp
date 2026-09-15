#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "Styling/SlateBrush.h"

#include "AgentMcpTestbedTypes.generated.h"

class UTexture2D;

/** Rarity of a smoke test row. Replaces the free-form Group name. */
UENUM(BlueprintType)
enum class EAgentMcpTestbedRarity : uint8
{
	Common,
	Rare,
	Epic,
};

/** Row struct of the smoke test DataTable fixture (/Game/AgentMcpFixtures/DT_AgentMcpSmoke). */
USTRUCT(BlueprintType)
struct AGENTMCPTESTBED_API FAgentMcpTestbedRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke")
	FString Label;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke")
	int32 Count = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke")
	float Weight = 1.0f;

	/** Free-form group name. Superseded by Rarity; kept until every table has been migrated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke")
	FName Group;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke")
	EAgentMcpTestbedRarity Rarity = EAgentMcpTestbedRarity::Common;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke")
	FVector Offset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke")
	TArray<FName> Keywords;
};

/** Data asset class of the smoke test checks for asset_create and for object_set_properties on assets. */
UCLASS(BlueprintType)
class AGENTMCPTESTBED_API UAgentMcpTestbedDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke")
	FLinearColor Color = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke")
	int32 Count = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke")
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke")
	FSlateBrush Brush;

	/** Named values; the smoke test checks that map keys keep their case in results. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke")
	TMap<FName, int32> Tokens;
};
