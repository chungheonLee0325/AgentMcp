#pragma once

#include "AgentMcpChatSettings.h"

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SCompoundWidget.h"

class SMultiLineEditableTextBox;
class SScrollBox;
class SVerticalBox;

namespace UE::AgentMcp::Chat
{
	class FChatConnections;
	class FChatSession;
	struct FChatChoice;
	struct FChatConversation;
	struct FChatEntry;
	struct FPendingApproval;
}

/**
 * The chat tab. Questions sit on the right and answers on the left, like other chat apps. Each answer keeps its tool calls and
 * in-between messages in a collapsed "work" section, so the final answer is what shows.
 */
class SAgentMcpChatPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAgentMcpChatPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SAgentMcpChatPanel() override;

private:
	/** The widgets of one question and its answer. */
	struct FTurn
	{
		TSharedPtr<SVerticalBox> Steps;
		TSharedPtr<SVerticalBox> Answer;
		/** The text shown as the answer; it moves into the steps when the model goes on with more tool calls. */
		FString AnswerText;
		FString AnswerSpeaker;
		int32 StepCount = 0;
		int32 ToolCount = 0;
		bool bActive = true;
	};

	void AddEntry(const UE::AgentMcp::Chat::FChatEntry& Entry);
	/** A card with the change, and approve and deny buttons, in the current answer. */
	void AddApprovalCard(const TSharedRef<UE::AgentMcp::Chat::FPendingApproval>& Approval);

	FText GetModeText() const;
	void OnModePicked(TSharedPtr<EAgentMcpChatMode> Item, ESelectInfo::Type SelectInfo);

	/** Installation and sign-in state of each provider, with connect and sign-out buttons. */
	TSharedRef<SWidget> MakeConnectionPanel();
	void RebuildConnectionRows();
	void ConnectProvider(EAgentMcpChatProvider Provider);

	/** The list of saved conversations on the left. */
	TSharedRef<SWidget> MakeHistoryPanel();
	void RefreshConversationList();
	void StartNewConversation();
	void SwitchToConversation(const TSharedRef<UE::AgentMcp::Chat::FChatConversation>& Target);
	void DeleteConversation(const TSharedRef<UE::AgentMcp::Chat::FChatConversation>& Target);
	/** Adds an entry to the current conversation and saves it. */
	void RecordEntry(const UE::AgentMcp::Chat::FChatEntry& Entry);
	void SaveConversation();
	/** The model and effort lists depend on the provider; rebuilt when it changes. */
	void RefreshProviderOptions();
	void RefreshEffortOptions();
	/** The effort levels of the current provider, and for Antigravity of the chosen model. */
	TArray<UE::AgentMcp::Chat::FChatChoice> GetCurrentEffortChoices() const;
	void ClearMessages();
	void AddNote(const FString& Text, bool bError);
	TSharedRef<FTurn> StartTurn(const FString* Question);
	void AddStep(FTurn& Turn, const TSharedRef<SWidget>& Widget);
	/** Moves the shown answer into the steps, because more work followed it. */
	void DemoteAnswer(FTurn& Turn);
	void ScrollToEnd();

	void Submit();
	FReply OnSendClicked();
	FReply OnNewChatClicked();
	FReply OnSettingsClicked();
	FReply OnInputKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent);

	FText GetProviderText() const;
	void OnProviderSelected(TSharedPtr<EAgentMcpChatProvider> Item, ESelectInfo::Type SelectInfo);

	FText GetModelText() const;
	FString GetModelLabel(const FString& Id) const;
	void OnModelListOpening();
	/** Loads the model list of the current provider in the background. */
	void LoadModels();
	void OnModelPicked(TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo);

	FText GetEffortText() const;
	void OnEffortPicked(TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo);

	FText GetSendText() const;
	FText GetStatusText() const;

	TSharedPtr<UE::AgentMcp::Chat::FChatSession> Session;
	TSharedPtr<FTurn> CurrentTurn;

	TArray<TSharedPtr<EAgentMcpChatProvider>> ProviderOptions;
	TArray<TSharedPtr<FString>> ModelOptions;
	TArray<TSharedPtr<FString>> EffortOptions;
	TArray<TSharedPtr<EAgentMcpChatMode>> ModeOptions;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ModelCombo;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> EffortCombo;
	FString ModelListMessage;
	bool bModelsLoaded = false;
	bool bModelsLoading = false;

	TSharedPtr<UE::AgentMcp::Chat::FChatConversation> Conversation;
	TArray<TSharedRef<UE::AgentMcp::Chat::FChatConversation>> Conversations;
	TSharedPtr<SVerticalBox> ConversationList;
	bool bShowHistory = true;
	TSharedPtr<UE::AgentMcp::Chat::FChatConnections> Connections;
	TSharedPtr<SVerticalBox> ConnectionRows;
	bool bShowConnections = false;
	/** A connect button started a setup; its provider is checked again when the setup ends. */
	bool bSetupRunning = false;
	/** Entries of a saved conversation are being shown again; they are not recorded a second time. */
	bool bReplaying = false;

	TSharedPtr<SScrollBox> MessageScroll;
	TSharedPtr<SVerticalBox> MessageList;
	TSharedPtr<SMultiLineEditableTextBox> InputBox;
};
