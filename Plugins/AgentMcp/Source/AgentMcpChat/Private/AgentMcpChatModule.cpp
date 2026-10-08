#include "AgentMcpChatPermissions.h"
#include "AgentMcpChatSession.h"
#include "SAgentMcpChatPanel.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

namespace AgentMcpChatModulePrivate
{
	const FName ChatTabName(TEXT("AgentMcpChat"));

	/** The conversation of AgentMcpChat.Ask, kept across calls like the panel's. */
	TSharedPtr<UE::AgentMcp::Chat::FChatSession> ConsoleSession;

	const TCHAR* GetKindName(UE::AgentMcp::Chat::EChatEntryKind Kind)
	{
		using UE::AgentMcp::Chat::EChatEntryKind;
		switch (Kind)
		{
		case EChatEntryKind::User: return TEXT("User");
		case EChatEntryKind::Assistant: return TEXT("Assistant");
		case EChatEntryKind::Tool: return TEXT("Tool");
		case EChatEntryKind::Error: return TEXT("Error");
		default: return TEXT("Status");
		}
	}

	FAutoConsoleCommand AskCommand(
		TEXT("AgentMcpChat.Ask"),
		TEXT("Sends a question with the current chat settings and logs the conversation (LogAgentMcpChat), without the panel. ")
		TEXT("For checking a provider or a model. AgentMcpChat.Ask reset starts over."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			using namespace UE::AgentMcp::Chat;
			const FString Question = FString::Join(Args, TEXT(" "));
			if (!ConsoleSession || Question == TEXT("reset"))
			{
				ConsoleSession = MakeShared<FChatSession>();
				ConsoleSession->OnEntry = [](const FChatEntry& Entry)
				{
					UE_LOG(LogAgentMcpChat, Display, TEXT("[%s] %s"), GetKindName(Entry.Kind), *Entry.Text);
				};
			}
			if (Question != TEXT("reset"))
			{
				ConsoleSession->Send(Question);
			}
		}));
}

class FAgentMcpChatModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		if (!GIsEditor || IsRunningCommandlet())
		{
			return;
		}

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(AgentMcpChatModulePrivate::ChatTabName,
			FOnSpawnTab::CreateRaw(this, &FAgentMcpChatModule::SpawnChatTab))
			.SetDisplayName(FText::FromString(TEXT("AI 채팅")))
			.SetTooltipText(FText::FromString(TEXT("AI 모델에게 이 프로젝트에 대해 물어봅니다. 모델은 읽기 전용 Agent MCP 도구로 내용을 찾아봅니다.")))
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Comment"))
			.SetMenuType(ETabSpawnerMenuType::Hidden);

		// Changes asked for by the chat go through the panel for approval.
		UE::AgentMcp::Chat::FChatApprovals::Get().Register();

		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAgentMcpChatModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		AgentMcpChatModulePrivate::ConsoleSession.Reset();
		if (GIsEditor && !IsRunningCommandlet())
		{
			UE::AgentMcp::Chat::FChatApprovals::Get().Unregister();
		}
		if (UObjectInitialized())
		{
			UToolMenus::UnRegisterStartupCallback(this);
			UToolMenus::UnregisterOwner(this);
		}
		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AgentMcpChatModulePrivate::ChatTabName);
		}
	}

private:
	TSharedRef<SDockTab> SpawnChatTab(const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SAgentMcpChatPanel)
			];
	}

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);

		const FUIAction OpenTab(FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(AgentMcpChatModulePrivate::ChatTabName);
		}));
		const FSlateIcon Icon(FAppStyle::GetAppStyleSetName(), "Icons.Comment");

		if (UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window"))
		{
			FToolMenuSection& Section = WindowMenu->FindOrAddSection("AgentMcp", FText::FromString(TEXT("Agent MCP")));
			Section.AddMenuEntry("OpenAgentMcpChat", FText::FromString(TEXT("AI 채팅")),
				FText::FromString(TEXT("AI 모델에게 이 프로젝트에 대해 물어봅니다.")), Icon, OpenTab);
		}

		// A toolbar button, so people who never open the Window menu find the chat.
		if (UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User"))
		{
			FToolMenuSection& Section = Toolbar->FindOrAddSection("AgentMcp");
			FToolMenuEntry Entry = FToolMenuEntry::InitToolBarButton("OpenAgentMcpChat", OpenTab, FText::FromString(TEXT("AI 채팅")),
				FText::FromString(TEXT("AI 모델에게 이 프로젝트에 대해 물어봅니다.")), Icon);
			Section.AddEntry(Entry);
		}
	}
};

IMPLEMENT_MODULE(FAgentMcpChatModule, AgentMcpChat)
