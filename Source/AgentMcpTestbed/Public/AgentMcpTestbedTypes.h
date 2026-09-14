#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"

#include "AgentMcpTestbedTypes.generated.h"

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
