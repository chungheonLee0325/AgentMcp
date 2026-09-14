#include "AgentMcpEditorTools.h"

#include "AgentMcpLogBuffer.h"
#include "AgentMcpToolsCommon.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Modules/ModuleManager.h"
#include "Selection.h"
#include "ShaderCompiler.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

namespace UE::AgentMcp::EditorToolsPrivate
{
	FAgentMcpUndoState MakeUndoState()
	{
		FAgentMcpUndoState State;
		if (GEditor && GEditor->Trans)
		{
			State.bCanUndo = GEditor->Trans->CanUndo();
			State.bCanRedo = GEditor->Trans->CanRedo();
			State.UndoableCount = GEditor->Trans->GetQueueLength() - GEditor->Trans->GetUndoCount();
			if (State.bCanUndo)
			{
				State.UndoTitle = GEditor->Trans->GetUndoContext(/*bCheckWhetherUndoPossible=*/false).Title.ToString();
			}
		}
		return State;
	}

	bool RequireEditorTransactions()
	{
		if (!GEditor || !GEditor->Trans)
		{
			RaiseToolError(TEXT("EDITOR_UNAVAILABLE"), TEXT("The editor transaction system is not available."));
			return false;
		}
		if (GEditor->PlayWorld || GEditor->IsPlaySessionInProgress())
		{
			RaiseToolError(TEXT("PIE_ACTIVE"), TEXT("Undo and redo are unavailable while a play session is running."), TEXT("Stop the play session first (pie_stop)."));
			return false;
		}
		return true;
	}
}

FAgentMcpEditorState UAgentMcpEditorTools::GetState(int32 MaxListedItems)
{
	using namespace UE::AgentMcp::EditorToolsPrivate;

	const int32 ListLimit = FMath::Clamp(MaxListedItems, 0, 100);

	FAgentMcpEditorState State;
	State.Project = FApp::GetProjectName();
	State.EngineVersion = FEngineVersion::Current().ToString(EVersionComponent::Patch);
	State.LatestLogSequence = static_cast<int64>(FAgentMcpLogBuffer::Get().GetLatestSequence());

	if (!GEditor)
	{
		UE::AgentMcp::RaiseToolError(TEXT("EDITOR_UNAVAILABLE"), TEXT("GEditor is not available."));
		return State;
	}

	if (const UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
	{
		State.EditorWorld = EditorWorld->GetPackage()->GetName();
	}

	State.PlaySession = UE::AgentMcp::Tools::MakePlaySessionState();

	TArray<FString> DirtyPackageNames;
	for (TObjectIterator<UPackage> It; It; ++It)
	{
		const UPackage* Package = *It;
		if (!Package || !Package->IsDirty() || Package == GetTransientPackage() || Package->HasAnyPackageFlags(PKG_CompiledIn))
		{
			continue;
		}
		const FString PackageName = Package->GetName();
		if (PackageName.StartsWith(TEXT("/Temp/")) || PackageName.StartsWith(TEXT("/Script/")))
		{
			continue;
		}
		DirtyPackageNames.Add(PackageName);
	}
	DirtyPackageNames.Sort();
	State.DirtyPackageCount = DirtyPackageNames.Num();
	for (int32 Index = 0; Index < DirtyPackageNames.Num() && Index < ListLimit; ++Index)
	{
		State.DirtyPackages.Add(DirtyPackageNames[Index]);
	}

	if (USelection* Selection = GEditor->GetSelectedActors())
	{
		for (FSelectionIterator It(*Selection); It; ++It)
		{
			if (const AActor* Actor = Cast<AActor>(*It))
			{
				++State.SelectedActorCount;
				if (State.SelectedActors.Num() < ListLimit)
				{
					State.SelectedActors.Add(Actor->GetActorLabel());
				}
			}
		}
	}

	State.Undo = MakeUndoState();
	State.ShaderJobsRemaining = GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
	State.bAssetRegistryLoading = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().IsLoadingAssets();

	const UE::AgentMcp::FAgentMcpRuntimeInfo RuntimeInfo = UE::AgentMcp::GetRuntimeInfo();
	State.McpServer.bRunning = RuntimeInfo.bServerRunning;
	State.McpServer.Endpoint = RuntimeInfo.EndpointUrl;
	State.McpServer.ExposedTools = RuntimeInfo.ExposedToolCount;
	State.McpServer.ExposureMode = RuntimeInfo.ExposureMode;
	State.McpServer.LastError = RuntimeInfo.LastError;

	// Other enabled plugins that present themselves as MCP servers. Two agents editing the same asset through different plugins
	// can overwrite each other's changes.
	for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
	{
		if (Plugin->GetName() == TEXT("AgentMcp"))
		{
			continue;
		}
		const FPluginDescriptor& Descriptor = Plugin->GetDescriptor();
		const bool bMcpPlugin = Plugin->GetName().Contains(TEXT("Mcp"))
			|| Descriptor.FriendlyName.Contains(TEXT("MCP"))
			|| Descriptor.Description.Contains(TEXT("Model Context Protocol"))
			|| Descriptor.Description.Contains(TEXT("MCP"), ESearchCase::CaseSensitive);
		if (bMcpPlugin)
		{
			FAgentMcpProviderState& ProviderState = State.OtherProviders.AddDefaulted_GetRef();
			ProviderState.Name = Plugin->GetName();
			ProviderState.Description = Descriptor.FriendlyName;
		}
	}

	return State;
}

FAgentMcpUndoResult UAgentMcpEditorTools::Undo()
{
	using namespace UE::AgentMcp::EditorToolsPrivate;

	FAgentMcpUndoResult Result;
	if (!RequireEditorTransactions())
	{
		return Result;
	}

	FText Reason;
	if (!GEditor->Trans->CanUndo(&Reason))
	{
		UE::AgentMcp::RaiseToolError(TEXT("NOTHING_TO_UNDO"), Reason.IsEmpty() ? TEXT("There is no transaction to undo.") : Reason.ToString());
		return Result;
	}

	const FString Title = GEditor->Trans->GetUndoContext(/*bCheckWhetherUndoPossible=*/false).Title.ToString();
	if (!GEditor->UndoTransaction())
	{
		UE::AgentMcp::RaiseToolError(TEXT("UNDO_FAILED"), FString::Printf(TEXT("The editor could not undo '%s'."), *Title));
		return Result;
	}

	Result.Transaction = Title;
	Result.Undo = MakeUndoState();
	return Result;
}

FAgentMcpUndoResult UAgentMcpEditorTools::Redo()
{
	using namespace UE::AgentMcp::EditorToolsPrivate;

	FAgentMcpUndoResult Result;
	if (!RequireEditorTransactions())
	{
		return Result;
	}

	FText Reason;
	if (!GEditor->Trans->CanRedo(&Reason))
	{
		UE::AgentMcp::RaiseToolError(TEXT("NOTHING_TO_REDO"), Reason.IsEmpty() ? TEXT("There is no transaction to redo.") : Reason.ToString());
		return Result;
	}

	if (!GEditor->RedoTransaction())
	{
		UE::AgentMcp::RaiseToolError(TEXT("REDO_FAILED"), TEXT("The editor could not redo the last undone transaction."));
		return Result;
	}

	// After a redo, the redone transaction is back on top of the undo stack.
	Result.Undo = MakeUndoState();
	Result.Transaction = Result.Undo.UndoTitle;
	return Result;
}
