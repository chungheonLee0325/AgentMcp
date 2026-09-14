#include "AgentMcpSampleTools.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

FAgentMcpSampleShowWidgetResult UAgentMcpSampleTools::ShowWidget(TSubclassOf<UUserWidget> WidgetClass, int32 ZOrder, bool bRemoveOtherWidgets)
{
	FAgentMcpSampleShowWidgetResult Result;
	if (!WidgetClass)
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("'widgetClass' is required."));
		return Result;
	}
	if (WidgetClass->HasAnyClassFlags(CLASS_Abstract))
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("%s is abstract."), *WidgetClass->GetName()),
			TEXT("Pass the class of a Widget Blueprint; its name ends in _C."));
		return Result;
	}

	UWorld* PlayWorld = GEditor ? GEditor->PlayWorld.Get() : nullptr;
	APlayerController* PlayerController = PlayWorld ? PlayWorld->GetFirstPlayerController() : nullptr;
	if (!PlayerController)
	{
		UE::AgentMcp::RaiseToolError(TEXT("PIE_NOT_ACTIVE"), TEXT("No play session with a local player is running."), TEXT("Start one with pie_start."));
		return Result;
	}

	if (bRemoveOtherWidgets)
	{
		TArray<UUserWidget*> Existing;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(PlayWorld, Existing, UUserWidget::StaticClass(), /*TopLevelOnly=*/true);
		for (UUserWidget* Existingwidget : Existing)
		{
			if (Existingwidget && Existingwidget->IsInViewport())
			{
				Existingwidget->RemoveFromParent();
				++Result.RemovedWidgets;
			}
		}
	}

	UUserWidget* Widget = CreateWidget<UUserWidget>(PlayerController, WidgetClass);
	if (!Widget)
	{
		UE::AgentMcp::RaiseToolError(TEXT("WIDGET_CREATE_FAILED"), FString::Printf(TEXT("%s could not be created."), *WidgetClass->GetName()),
			TEXT("Compile the Widget Blueprint (blueprint_compile) and check log_get_recent."));
		return Result;
	}
	Widget->AddToViewport(ZOrder);
	Result.WidgetClass = WidgetClass->GetPathName();
	return Result;
}
