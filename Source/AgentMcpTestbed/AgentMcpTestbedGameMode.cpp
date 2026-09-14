#include "AgentMcpTestbedGameMode.h"

#include "AgentMcpTestbedTypes.h"
#include "AgentMcpTestbedWidget.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/PackageName.h"
#include "TimerManager.h"
#include "UObject/SoftObjectPath.h"

namespace AgentMcpTestbedGameModePrivate
{
	const TCHAR* const DataTablePackage = TEXT("/Game/AgentMcpFixtures/DT_AgentMcpSmoke");

	/** Row counts by rarity of the fixture DataTable, such as "Common=2 Rare=1 Epic=0"; empty when the table does not exist. */
	FString MakeRaritySummary()
	{
		if (!FPackageName::DoesPackageExist(DataTablePackage))
		{
			return FString();
		}
		const UDataTable* DataTable = Cast<UDataTable>(FSoftObjectPath(FString(DataTablePackage) + TEXT(".DT_AgentMcpSmoke")).TryLoad());
		const UEnum* RarityEnum = StaticEnum<EAgentMcpTestbedRarity>();
		if (!DataTable || DataTable->GetRowStruct() != FAgentMcpTestbedRow::StaticStruct() || !RarityEnum)
		{
			return FString();
		}

		TArray<int32> Counts;
		Counts.SetNumZeroed(RarityEnum->NumEnums() - 1); // The last entry is the generated _MAX value.
		DataTable->ForeachRow<FAgentMcpTestbedRow>(TEXT("AgentMcpTestbedGameMode"), [&Counts](const FName&, const FAgentMcpTestbedRow& Row)
		{
			const int32 Index = static_cast<int32>(Row.Rarity);
			if (Counts.IsValidIndex(Index))
			{
				++Counts[Index];
			}
		});

		TArray<FString> Parts;
		for (int32 Index = 0; Index < Counts.Num(); ++Index)
		{
			Parts.Add(FString::Printf(TEXT("%s=%d"), *RarityEnum->GetNameStringByIndex(Index), Counts[Index]));
		}
		return FString::Join(Parts, TEXT(" "));
	}
}

void AAgentMcpTestbedGameMode::BeginPlay()
{
	Super::BeginPlay();

	const FString RaritySummary = AgentMcpTestbedGameModePrivate::MakeRaritySummary();
	if (!RaritySummary.IsEmpty())
	{
		UE_LOG(LogTemp, Display, TEXT("AgentMcp testbed rows by rarity: %s"), *RaritySummary);
	}

	// Wait one tick so that the local player controller exists.
	GetWorldTimerManager().SetTimerForNextTick(this, &AAgentMcpTestbedGameMode::ShowHud);
}

void AAgentMcpTestbedGameMode::ShowHud()
{
	// testbed_reset_fixtures creates the widget. Check the package first so that a fresh testbed does not log a failed load.
	const FString WidgetPackage = TEXT("/Game/AgentMcpFixtures/WBP_AgentMcpBound");
	UClass* WidgetClass = FPackageName::DoesPackageExist(WidgetPackage)
		? FSoftClassPath(WidgetPackage + TEXT(".WBP_AgentMcpBound_C")).TryLoadClass<UUserWidget>()
		: nullptr;
	APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!WidgetClass || !PlayerController)
	{
		UE_LOG(LogTemp, Display, TEXT("AgentMcp testbed HUD not shown: widget class %s, player controller %s."),
			WidgetClass ? TEXT("found") : TEXT("missing"), PlayerController ? TEXT("found") : TEXT("missing"));
		return;
	}

	if (UUserWidget* Widget = CreateWidget<UUserWidget>(PlayerController, WidgetClass))
	{
		// Show the row counts by rarity in the HUD title.
		const FString RaritySummary = AgentMcpTestbedGameModePrivate::MakeRaritySummary();
		UAgentMcpTestbedWidget* TestbedWidget = Cast<UAgentMcpTestbedWidget>(Widget);
		if (TestbedWidget && TestbedWidget->TitleText && !RaritySummary.IsEmpty())
		{
			const FString Title = FString::Printf(TEXT("AgentMcp testbed HUD - %s"), *RaritySummary);
			TestbedWidget->TitleText->SetText(FText::FromString(Title));
			UE_LOG(LogTemp, Display, TEXT("AgentMcp testbed HUD title: %s"), *Title);
		}

		Widget->AddToViewport();
		UE_LOG(LogTemp, Display, TEXT("AgentMcp testbed HUD shown: %s"), *WidgetClass->GetPathName());
	}
}
