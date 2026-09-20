#include "AgentMcpTestbedFixtureTools.h"

#include "AgentMcpTestbedTypes.h"
#include "AgentMcpTestbedWidget.h"

#include "AssetToolsModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Factories/DataTableFactory.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "IAssetTools.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"

namespace AgentMcpTestbedPrivate
{
	const TCHAR* const FixtureFolder = TEXT("/Game/AgentMcpFixtures");
	const TCHAR* const DataTableName = TEXT("DT_AgentMcpSmoke");
	const TCHAR* const BoundWidgetName = TEXT("WBP_AgentMcpBound");
	const TCHAR* const MissingBindingWidgetName = TEXT("WBP_AgentMcpMissingBinding");
	/** Created by the smoke test with umg_create_widget_blueprint; every reset deletes it again. */
	const TCHAR* const AuthoringWidgetName = TEXT("WBP_AgentMcpAuthoring");

	/** Object path of a fixture asset; AssetName may start with a subfolder, as in Copy/WBP_Name. */
	FString MakeObjectPath(const TCHAR* AssetName)
	{
		return FString::Printf(TEXT("%s/%s.%s"), FixtureFolder, AssetName, *FPackageName::GetShortName(FString(AssetName)));
	}

	IAssetTools& GetAssetTools()
	{
		return FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	}

	/** Deletes a fixture asset in memory and on disk, without dialogs; adds its path to OutDeleted when it existed. */
	void DeleteFixtureAsset(const TCHAR* AssetName, TArray<FString>& OutDeleted)
	{
		const FString ObjectPath = MakeObjectPath(AssetName);
		UObject* Asset = StaticFindObject(UObject::StaticClass(), nullptr, *ObjectPath);
		if (!IsValid(Asset) && FPackageName::DoesPackageExist(FString::Printf(TEXT("%s/%s"), FixtureFolder, AssetName)))
		{
			Asset = LoadObject<UObject>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		if (IsValid(Asset) && ObjectTools::ForceDeleteObjects({ Asset }, /*ShowConfirmation=*/false) > 0)
		{
			OutDeleted.Add(ObjectPath);
		}
	}

	void AddRow(UDataTable* DataTable, const TCHAR* RowName, const TCHAR* Label, int32 Count, float Weight, const TCHAR* Group, const FVector& Offset, const TArray<FName>& Keywords)
	{
		FAgentMcpTestbedRow Row;
		Row.Label = Label;
		Row.Count = Count;
		Row.Weight = Weight;
		Row.Group = FName(Group);
		Row.Rarity = FCString::Stricmp(Group, TEXT("Rare")) == 0 ? EAgentMcpTestbedRarity::Rare : EAgentMcpTestbedRarity::Common;
		Row.Offset = Offset;
		Row.Keywords = Keywords;
		DataTable->AddRow(FName(RowName), Row);
	}

	UDataTable* ResetDataTable(TArray<FString>& OutCreated)
	{
		UDataTable* DataTable = LoadObject<UDataTable>(nullptr, *MakeObjectPath(DataTableName), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!DataTable)
		{
			UDataTableFactory* Factory = NewObject<UDataTableFactory>();
			Factory->Struct = FAgentMcpTestbedRow::StaticStruct();
			DataTable = Cast<UDataTable>(GetAssetTools().CreateAsset(DataTableName, FixtureFolder, UDataTable::StaticClass(), Factory));
			if (DataTable)
			{
				OutCreated.Add(DataTable->GetPathName());
			}
		}
		if (DataTable)
		{
			DataTable->EmptyTable();
			AddRow(DataTable, TEXT("Alpha"), TEXT("First"), 1, 0.5f, TEXT("Common"), FVector(1.0, 2.0, 3.0), { FName(TEXT("red")) });
			AddRow(DataTable, TEXT("Beta"), TEXT("Second"), 2, 1.5f, TEXT("Rare"), FVector::ZeroVector, {});
			AddRow(DataTable, TEXT("Gamma"), TEXT("Third"), 3, 2.5f, TEXT("Common"), FVector(0.0, 0.0, 10.0), { FName(TEXT("blue")), FName(TEXT("green")) });
			DataTable->MarkPackageDirty();
		}
		return DataTable;
	}

	/** Creates the Widget Blueprint if needed, adds or removes the TitleText binding, and compiles it. */
	UWidgetBlueprint* ResetWidgetBlueprint(const TCHAR* AssetName, bool bWithTitle, TArray<FString>& OutCreated)
	{
		UWidgetBlueprint* WidgetBlueprint = LoadObject<UWidgetBlueprint>(nullptr, *MakeObjectPath(AssetName), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!WidgetBlueprint)
		{
			UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
			Factory->BlueprintType = BPTYPE_Normal;
			Factory->ParentClass = UAgentMcpTestbedWidget::StaticClass();
			WidgetBlueprint = Cast<UWidgetBlueprint>(GetAssetTools().CreateAsset(AssetName, FixtureFolder, UWidgetBlueprint::StaticClass(), Factory));
			if (!WidgetBlueprint)
			{
				return nullptr;
			}
			OutCreated.Add(WidgetBlueprint->GetPathName());
		}

		UWidgetTree* WidgetTree = WidgetBlueprint->WidgetTree;
		// UWidgetBlueprintFactory adds a root only when UUMGEditorProjectSettings::DefaultRootWidget is set, and it may be any panel.
		UPanelWidget* Root = Cast<UPanelWidget>(WidgetTree->RootWidget);
		if (!Root)
		{
			Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
			WidgetTree->RootWidget = Root;
		}

		UWidget* Title = WidgetTree->FindWidget(TEXT("TitleText"));
		if (bWithTitle && !Title)
		{
			UTextBlock* TextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
			TextBlock->SetText(FText::FromString(TEXT("AgentMcp testbed HUD")));
			TextBlock->bIsVariable = true;
			if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Root->AddChild(TextBlock)))
			{
				CanvasSlot->SetPosition(FVector2D(64.0, 64.0));
				CanvasSlot->SetAutoSize(true);
			}
		}
		else if (!bWithTitle && Title)
		{
			WidgetTree->RemoveWidget(Title);
		}

		// Solid magenta block that the smoke test looks for in play session captures with UI.
		if (bWithTitle && !WidgetTree->FindWidget(TEXT("UiProbe")))
		{
			UImage* Probe = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("UiProbe"));
			Probe->SetColorAndOpacity(FLinearColor(1.0f, 0.0f, 1.0f, 1.0f));
			if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Probe)))
			{
				CanvasSlot->SetPosition(FVector2D(64.0, 160.0));
				CanvasSlot->SetSize(FVector2D(320.0, 160.0));
			}
		}

		// A newly created Widget Blueprint reports a missing required BindWidget only as a warning (WidgetBlueprintCompiler.cpp checks
		// the transient bIsNewlyCreated). Compile the fixture like an asset loaded from disk, where it is an error.
		WidgetBlueprint->bIsNewlyCreated = false;
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
		// Compiling also sets bDisplayCompilePIEWarning again (BlueprintCompilationManager.cpp).
		FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint, EBlueprintCompileOptions::SkipGarbageCollection);
		return WidgetBlueprint;
	}
}

FAgentMcpTestbedFixtures UAgentMcpTestbedFixtureTools::ResetFixtures()
{
	using namespace AgentMcpTestbedPrivate;

	FAgentMcpTestbedFixtures Result;

	// Clear the undo history so that transactions of earlier runs cannot restore other fixture states.
	if (GEditor)
	{
		GEditor->ResetTransaction(FText::FromString(TEXT("AgentMcp testbed fixtures reset")));
	}

	// The smoke test creates levels with level_new. A level that is still open cannot be deleted, and its unsaved changes are
	// fixture state, so the editor goes back to an empty map without asking.
	if (GEditor)
	{
		const UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
		if (EditorWorld && EditorWorld->GetPackage() && EditorWorld->GetPackage()->GetName().StartsWith(FixtureFolder))
		{
			TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);
			GEditor->NewMap(/*bIsPartitionedWorld=*/false);
			// An empty map has no static mesh, which the actor checks expect from the startup map, so the fixture puts a floor back.
			UEditorActorSubsystem* ActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
			UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"), nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (AActor* FloorActor = ActorSubsystem && Cube ? ActorSubsystem->SpawnActorFromObject(Cube, FVector::ZeroVector) : nullptr)
			{
				FloorActor->SetActorLabel(TEXT("Floor"));
				FloorActor->SetActorScale3D(FVector(20.0, 20.0, 1.0));
				Result.Created.Add(FloorActor->GetPathName());
			}
		}
	}
	DeleteFixtureAsset(TEXT("Maps/L_AgentMcpSmoke"), Result.Deleted);
	DeleteFixtureAsset(TEXT("Maps/L_AgentMcpSmokeSecond"), Result.Deleted);

	// Without undo history nothing refers to the authored Widget Blueprints any more, so they can be deleted. The part is nested in
	// the authoring Widget Blueprint, so it goes second.
	DeleteFixtureAsset(AuthoringWidgetName, Result.Deleted);
	DeleteFixtureAsset(TEXT("WBP_AgentMcpAuthoringPart"), Result.Deleted);
	DeleteFixtureAsset(TEXT("Copy/WBP_AgentMcpAuthoringPart"), Result.Deleted);
	// Created with asset_create and asset_import_textures. The data asset refers to the texture, so it goes first.
	DeleteFixtureAsset(TEXT("DA_AgentMcpSmoke"), Result.Deleted);
	DeleteFixtureAsset(TEXT("DT_AgentMcpCreated"), Result.Deleted);
	DeleteFixtureAsset(TEXT("T_AgentMcpSmokeIcon"), Result.Deleted);

	UDataTable* DataTable = ResetDataTable(Result.Created);
	UWidgetBlueprint* BoundWidget = ResetWidgetBlueprint(BoundWidgetName, /*bWithTitle=*/true, Result.Created);
	UWidgetBlueprint* MissingBindingWidget = ResetWidgetBlueprint(MissingBindingWidgetName, /*bWithTitle=*/false, Result.Created);
	if (!DataTable || !BoundWidget || !MissingBindingWidget)
	{
		UE::AgentMcp::RaiseToolError(TEXT("FIXTURE_FAILED"), TEXT("Could not create the testbed fixtures."));
		return Result;
	}

	TArray<UPackage*> Packages;
	Packages.Add(DataTable->GetPackage());
	Packages.Add(BoundWidget->GetPackage());
	Packages.Add(MissingBindingWidget->GetPackage());
	Result.bSaved = UEditorLoadingAndSavingUtils::SavePackages(Packages, /*bOnlyDirty=*/false);

	Result.DataTable = DataTable->GetPathName();
	for (const TPair<FName, uint8*>& Row : DataTable->GetRowMap())
	{
		Result.Rows.Add(Row.Key.ToString());
	}
	Result.WidgetBlueprints.Add(BoundWidget->GetPathName());
	Result.WidgetBlueprints.Add(MissingBindingWidget->GetPathName());
	return Result;
}

FAgentMcpTestbedHookResult UAgentMcpTestbedFixtureTools::WriteThenFail(AActor* Actor)
{
	FAgentMcpTestbedHookResult Result;
	if (!Actor)
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("'actor' is required."));
		return Result;
	}

	Actor->Modify();
	Actor->Tags.Add(FName(TEXT("AgentMcpRollbackProbe")));
	UE::AgentMcp::RaiseToolError(TEXT("TESTBED_FAILURE"), FString::Printf(TEXT("Failed on purpose after adding a tag to %s."), *Actor->GetActorLabel()));
	return Result;
}

UAgentMcpAsyncResult* UAgentMcpTestbedFixtureTools::WaitSeconds(float Seconds)
{
	const float WaitTime = FMath::Clamp(Seconds, 0.0f, 120.0f);
	const double EndTime = FPlatformTime::Seconds() + WaitTime;
	return UAgentMcpAsyncResult::Create(WaitTime + 5.0f, [EndTime](UAgentMcpAsyncResult& Result)
	{
		if (FPlatformTime::Seconds() >= EndTime)
		{
			FAgentMcpTestbedHookResult Done;
			Done.Message = TEXT("The wait finished.");
			Result.CompleteWith(Done);
		}
	});
}

FAgentMcpTestbedHookResult UAgentMcpTestbedFixtureTools::SetPieWarning(UBlueprint* Blueprint, bool bEnabled)
{
	FAgentMcpTestbedHookResult Result;
	if (!Blueprint)
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("'blueprint' is required."));
		return Result;
	}

	Blueprint->bDisplayCompilePIEWarning = bEnabled;
	Result.Message = FString::Printf(TEXT("%s: PIE confirmation %s."), *Blueprint->GetPathName(), bEnabled ? TEXT("enabled") : TEXT("disabled"));
	return Result;
}
