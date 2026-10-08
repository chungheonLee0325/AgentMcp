#include "SAgentMcpChatPanel.h"

#include "AgentMcpChatAntigravity.h"
#include "AgentMcpChatClaudeCode.h"
#include "AgentMcpChatCodex.h"
#include "AgentMcpChatConnections.h"
#include "AgentMcpChatHistory.h"
#include "AgentMcpChatMarkdown.h"
#include "AgentMcpChatPermissions.h"
#include "AgentMcpChatProviders.h"
#include "AgentMcpChatSession.h"

#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Text/RichTextLayoutMarshaller.h"
#include "ISettingsModule.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/STextBlock.h"

using namespace UE::AgentMcp::Chat;

namespace AgentMcpChatPanelPrivate
{
	const FLinearColor UserColor(0.10f, 0.22f, 0.40f);
	const FLinearColor AnswerColor(0.15f, 0.15f, 0.15f);
	const FLinearColor ErrorColor(0.40f, 0.08f, 0.08f);
	const FLinearColor ApprovalColor(0.38f, 0.28f, 0.06f);
	const FLinearColor DestructiveColor(0.45f, 0.10f, 0.06f);

	bool IsClaudeCode()
	{
		return GetDefault<UAgentMcpChatSettings>()->Provider == EAgentMcpChatProvider::ClaudeCode;
	}

	bool IsAntigravity()
	{
		return GetDefault<UAgentMcpChatSettings>()->Provider == EAgentMcpChatProvider::Antigravity;
	}

	bool IsCodex()
	{
		return GetDefault<UAgentMcpChatSettings>()->Provider == EAgentMcpChatProvider::Codex;
	}

	/** Providers that run a signed-in CLI, with a connect button and an effort picker. */
	bool IsCliProvider()
	{
		return IsClaudeCode() || IsAntigravity() || IsCodex();
	}

	const TArray<FChatChoice>& GetEffortChoices()
	{
		return IsAntigravity() ? GetAntigravityEffortChoices() : GetClaudeCodeEffortChoices();
	}

	FString& GetEffortSetting(UAgentMcpChatSettings& Settings)
	{
		switch (Settings.Provider)
		{
		case EAgentMcpChatProvider::Antigravity: return Settings.AntigravityEffort;
		case EAgentMcpChatProvider::Codex: return Settings.CodexEffort;
		default: return Settings.ClaudeCodeEffort;
		}
	}

	enum class ETextKind : uint8
	{
		Message,
		Note,
		Step,
		Tool,
	};

	/** Text styles of plain messages. The widgets keep a pointer, so they live as long as the module. */
	const FTextBlockStyle& GetTextStyle(ETextKind Kind)
	{
		auto Make = [](const ANSICHAR* Typeface, int32 Size, const FSlateColor& Color)
		{
			FTextBlockStyle Style = FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
			Style.SetFont(FCoreStyle::GetDefaultFontStyle(Typeface, Size));
			Style.SetColorAndOpacity(Color);
			return Style;
		};
		static const FTextBlockStyle Message = Make("Regular", 10, FSlateColor::UseForeground());
		static const FTextBlockStyle Note = Make("Italic", 9, FSlateColor::UseSubduedForeground());
		static const FTextBlockStyle Step = Make("Italic", 8, FSlateColor::UseSubduedForeground());
		static const FTextBlockStyle Tool = Make("Mono", 8, FSlateColor::UseSubduedForeground());
		switch (Kind)
		{
		case ETextKind::Note: return Note;
		case ETextKind::Step: return Step;
		case ETextKind::Tool: return Tool;
		default: return Message;
		}
	}

	/** Read-only editable text, so messages can be selected and copied. */
	TSharedRef<SWidget> MakePlainText(const FString& Text, ETextKind Kind)
	{
		return SNew(SMultiLineEditableText)
			.Text(FText::FromString(Text))
			.TextStyle(&GetTextStyle(Kind))
			.IsReadOnly(true)
			.AutoWrapText(true);
	}

	TSharedRef<SWidget> MakeMarkdownText(const FString& Markdown)
	{
		return SNew(SMultiLineEditableText)
			.Text(FText::FromString(MarkdownToRichText(Markdown)))
			.Marshaller(FRichTextLayoutMarshaller::Create(TArray<TSharedRef<ITextDecorator>>(), &GetChatTextStyles()))
			.TextStyle(&GetChatBodyTextStyle())
			.IsReadOnly(true)
			.AutoWrapText(true);
	}

	TSharedRef<SWidget> MakeBubble(const TSharedRef<SWidget>& Content, const FLinearColor& Color)
	{
		return SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(Color)
			.Padding(FMargin(10.0f, 6.0f))
			[
				Content
			];
	}
}

void SAgentMcpChatPanel::Construct(const FArguments& InArgs)
{
	using namespace AgentMcpChatPanelPrivate;

	for (EAgentMcpChatProvider Provider : { EAgentMcpChatProvider::ClaudeCode, EAgentMcpChatProvider::Codex, EAgentMcpChatProvider::Antigravity, EAgentMcpChatProvider::OpenAI,
		EAgentMcpChatProvider::Gemini, EAgentMcpChatProvider::OpenAICompatible })
	{
		ProviderOptions.Add(MakeShared<EAgentMcpChatProvider>(Provider));
	}
	for (EAgentMcpChatMode Mode : { EAgentMcpChatMode::ReadOnly, EAgentMcpChatMode::AssetEdit, EAgentMcpChatMode::CodeEdit,
		EAgentMcpChatMode::AssetAndCodeEdit })
	{
		ModeOptions.Add(MakeShared<EAgentMcpChatMode>(Mode));
	}

	// Older versions stored Claude Code aliases; the picker names full models, so the version shows.
	{
		UAgentMcpChatSettings* Settings = GetMutableDefault<UAgentMcpChatSettings>();
		static const TMap<FString, FString> Aliases = {
			{ TEXT("opus"), TEXT("claude-opus-5-5") }, { TEXT("sonnet"), TEXT("claude-sonnet-5-5") },
			{ TEXT("haiku"), TEXT("claude-haiku-5-5") }, { TEXT("fable"), TEXT("claude-fable-5-1") },
		};
		if (const FString* FullName = Aliases.Find(Settings->ClaudeCodeModel))
		{
			Settings->ClaudeCodeModel = *FullName;
			Settings->SaveConfig();
		}
	}

	Session = MakeShared<FChatSession>();
	TWeakPtr<SAgentMcpChatPanel> WeakSelf = SharedThis(this);
	// When an answer ends, later notes (setup progress, errors before a question) belong to no question.
	Session->OnStateChanged = [WeakSelf]()
	{
		const TSharedPtr<SAgentMcpChatPanel> This = WeakSelf.Pin();
		if (This && !This->Session->IsBusy())
		{
			if (This->CurrentTurn)
			{
				This->CurrentTurn->bActive = false;
			}
			// The CLI conversation ids arrive during the answer; keep them with the conversation once it is done.
			This->SaveConversation();
			if (This->bSetupRunning)
			{
				This->bSetupRunning = false;
				This->Connections->Refresh(GetDefault<UAgentMcpChatSettings>()->Provider);
			}
		}
	};
	Session->OnEntry = [WeakSelf](const FChatEntry& Entry)
	{
		if (const TSharedPtr<SAgentMcpChatPanel> This = WeakSelf.Pin())
		{
			This->AddEntry(Entry);
			This->RecordEntry(Entry);
		}
	};
	Conversation = History::Create();
	Conversations = History::LoadAll();
	Connections = MakeShared<FChatConnections>();
	Connections->OnChanged = [WeakSelf]()
	{
		if (const TSharedPtr<SAgentMcpChatPanel> This = WeakSelf.Pin())
		{
			This->RebuildConnectionRows();
		}
	};
	FChatApprovals::Get().OnRequest = [WeakSelf](const TSharedRef<FPendingApproval>& Approval)
	{
		if (const TSharedPtr<SAgentMcpChatPanel> This = WeakSelf.Pin())
		{
			This->AddApprovalCard(Approval);
		}
		else
		{
			Approval->Resolve(false, TEXT("The chat panel closed."));
		}
	};

	const EAgentMcpChatProvider CurrentProvider = GetDefault<UAgentMcpChatSettings>()->Provider;
	TSharedPtr<EAgentMcpChatProvider> InitialProvider = ProviderOptions[0];
	for (const TSharedPtr<EAgentMcpChatProvider>& Option : ProviderOptions)
	{
		if (*Option == CurrentProvider)
		{
			InitialProvider = Option;
		}
	}

	ChildSlot
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			MakeHistoryPanel()
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SComboBox<TSharedPtr<EAgentMcpChatProvider>>)
				.OptionsSource(&ProviderOptions)
				.InitiallySelectedItem(InitialProvider)
				.OnSelectionChanged(this, &SAgentMcpChatPanel::OnProviderSelected)
				.OnGenerateWidget_Lambda([](TSharedPtr<EAgentMcpChatProvider> Item)
				{
					return SNew(STextBlock).Text(FText::FromString(GetProviderLabel(*Item)));
				})
				[
					SNew(STextBlock).Text(this, &SAgentMcpChatPanel::GetProviderText)
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(ModelCombo, SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&ModelOptions)
				.ToolTipText(FText::FromString(TEXT("이 대화에 쓸 모델입니다.")))
				.OnComboBoxOpening(this, &SAgentMcpChatPanel::OnModelListOpening)
				.OnSelectionChanged(this, &SAgentMcpChatPanel::OnModelPicked)
				.OnGenerateWidget_Lambda([this](TSharedPtr<FString> Item)
				{
					return SNew(STextBlock).Text(FText::FromString(GetModelLabel(*Item)));
				})
				[
					SNew(STextBlock).Text(this, &SAgentMcpChatPanel::GetModelText)
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(EffortCombo, SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&EffortOptions)
				.ToolTipText(FText::FromString(TEXT("추론 깊이입니다. 높을수록 더 오래 생각하고 느려집니다.")))
				.Visibility_Lambda([this]() { return IsCliProvider() && EffortOptions.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed; })
				.OnSelectionChanged(this, &SAgentMcpChatPanel::OnEffortPicked)
				.OnGenerateWidget_Lambda([this](TSharedPtr<FString> Item)
				{
					// The button says what it is; the items only name the level.
					return SNew(STextBlock).Text(FText::FromString(GetChoiceLabel(GetCurrentEffortChoices(), *Item).Replace(TEXT("추론: "), TEXT(""))));
				})
				[
					SNew(STextBlock).Text(this, &SAgentMcpChatPanel::GetEffortText)
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SComboBox<TSharedPtr<EAgentMcpChatMode>>)
				.OptionsSource(&ModeOptions)
				.ToolTipText(FText::FromString(TEXT("모델이 무엇을 바꿀 수 있는지 정합니다. 어떤 모드든 바꾸기 전에 이 패널에서 승인을 받습니다.")))
				.OnSelectionChanged(this, &SAgentMcpChatPanel::OnModePicked)
				.OnGenerateWidget_Lambda([](TSharedPtr<EAgentMcpChatMode> Item)
				{
					return SNew(STextBlock).Text(FText::FromString(GetModeLabel(*Item)));
				})
				[
					SNew(STextBlock).Text(this, &SAgentMcpChatPanel::GetModeText)
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("대화 목록")))
				.ToolTipText(FText::FromString(TEXT("지난 대화 목록을 보이거나 숨깁니다.")))
				.OnClicked_Lambda([this]()
				{
					bShowHistory = !bShowHistory;
					return FReply::Handled();
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("새 대화")))
				.OnClicked(this, &SAgentMcpChatPanel::OnNewChatClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("계정")))
				.ToolTipText(FText::FromString(TEXT("AI 서비스(Claude Code, ChatGPT, Gemini)마다 설치와 로그인 상태를 보고, 로그인하거나 로그아웃합니다.")))
				.OnClicked_Lambda([this]()
				{
					bShowConnections = !bShowConnections;
					if (bShowConnections)
					{
						Connections->RefreshAll();
					}
					return FReply::Handled();
				})
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 0.0f, 4.0f, 4.0f)
		[
			MakeConnectionPanel()
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(4.0f, 0.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(2.0f)
			[
				SAssignNew(MessageScroll, SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(MessageList, SVerticalBox)
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(this, &SAgentMcpChatPanel::GetStatusText)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SBox)
				.MinDesiredHeight(48.0f)
				.MaxDesiredHeight(160.0f)
				[
					SAssignNew(InputBox, SMultiLineEditableTextBox)
					.AutoWrapText(true)
					.HintText_Lambda([this]()
					{
						return FText::FromString(Session->IsAwaitingInput()
							? TEXT("브라우저에 나온 로그인 코드를 붙여넣고 Enter를 누르세요.")
							: TEXT("이 프로젝트에 대해 물어보세요. Enter로 보내고 Shift+Enter로 줄을 바꿉니다."));
					})
					.OnKeyDownHandler(this, &SAgentMcpChatPanel::OnInputKeyDown)
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			.VAlign(VAlign_Bottom)
			[
				SNew(SButton)
				.Text(this, &SAgentMcpChatPanel::GetSendText)
				.OnClicked(this, &SAgentMcpChatPanel::OnSendClicked)
			]
		]
		]
	];

	LoadModels();
	RefreshEffortOptions();
	RefreshConversationList();
	RebuildConnectionRows();
	Connections->Refresh(GetDefault<UAgentMcpChatSettings>()->Provider);
	AddNote(TEXT("에셋, 레벨, 블루프린트, 위젯, 데이터 테이블, 에디터 로그, C++ 코드에 대해 물어볼 수 있습니다. 모델은 프로젝트를 읽기만 하고 바꾸지는 못합니다."), false);
}

SAgentMcpChatPanel::~SAgentMcpChatPanel()
{
	SaveConversation();
	// The session reports a stop while it is destroyed; this widget is already going away.
	Session->OnEntry = nullptr;
	Session->OnStateChanged = nullptr;
	Session.Reset();
	FChatApprovals::Get().OnRequest = nullptr;
	FChatApprovals::Get().DenyAll(TEXT("The chat panel closed."));
}

void SAgentMcpChatPanel::AddEntry(const FChatEntry& Entry)
{
	using namespace AgentMcpChatPanelPrivate;

	switch (Entry.Kind)
	{
	case EChatEntryKind::User:
		CurrentTurn = StartTurn(&Entry.Text);
		break;

	case EChatEntryKind::Assistant:
	{
		if (!CurrentTurn || !CurrentTurn->bActive)
		{
			CurrentTurn = StartTurn(nullptr);
		}
		FTurn& Turn = *CurrentTurn;
		DemoteAnswer(Turn);
		Turn.AnswerText = Entry.Text;
		Turn.AnswerSpeaker = Entry.Speaker;
		Turn.Answer->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 60.0f, 0.0f)
		[
			MakeBubble(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Entry.Speaker))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					MakeMarkdownText(Entry.Text)
				],
				AnswerColor)
		];
		break;
	}

	case EChatEntryKind::Tool:
		if (!CurrentTurn || !CurrentTurn->bActive)
		{
			AddNote(Entry.Text, false);
			break;
		}
		DemoteAnswer(*CurrentTurn);
		++CurrentTurn->ToolCount;
		AddStep(*CurrentTurn, MakePlainText(TEXT("> ") + Entry.Text, ETextKind::Tool));
		break;

	case EChatEntryKind::Error:
		if (CurrentTurn && CurrentTurn->bActive)
		{
			CurrentTurn->Answer->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 60.0f, 0.0f)
			[
				MakeBubble(MakePlainText(Entry.Text, ETextKind::Message), ErrorColor)
			];
		}
		else
		{
			AddNote(Entry.Text, true);
		}
		break;

	default:
		if (CurrentTurn && CurrentTurn->bActive && (Session->IsBusy() || bReplaying))
		{
			AddStep(*CurrentTurn, MakePlainText(Entry.Text, ETextKind::Step));
		}
		else
		{
			AddNote(Entry.Text, false);
		}
		break;
	}
	ScrollToEnd();
}

void SAgentMcpChatPanel::AddNote(const FString& Text, bool bError)
{
	using namespace AgentMcpChatPanelPrivate;

	if (CurrentTurn)
	{
		// A note after an answer belongs to no question; the next answer starts a new turn.
		CurrentTurn->bActive = false;
	}
	MessageList->AddSlot()
	.AutoHeight()
	.Padding(12.0f, 4.0f)
	[
		bError
			? MakeBubble(MakePlainText(Text, ETextKind::Message), ErrorColor)
			: MakePlainText(Text, ETextKind::Note)
	];
	ScrollToEnd();
}

TSharedRef<SAgentMcpChatPanel::FTurn> SAgentMcpChatPanel::StartTurn(const FString* Question)
{
	using namespace AgentMcpChatPanelPrivate;

	if (CurrentTurn)
	{
		CurrentTurn->bActive = false;
	}
	TSharedRef<FTurn> Turn = MakeShared<FTurn>();

	if (Question)
	{
		MessageList->AddSlot()
		.AutoHeight()
		.Padding(6.0f, 10.0f, 6.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(0.25f)
			[
				SNullWidget::NullWidget
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.75f)
			.HAlign(HAlign_Right)
			[
				MakeBubble(MakePlainText(*Question, ETextKind::Message), UserColor)
			]
		];
	}

	const TWeakPtr<FTurn> WeakTurn = Turn;
	MessageList->AddSlot()
	.AutoHeight()
	.Padding(6.0f, 2.0f)
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 60.0f, 2.0f)
		[
			SNew(SExpandableArea)
			.InitiallyCollapsed(true)
			.Visibility_Lambda([this, WeakTurn]()
			{
				const TSharedPtr<FTurn> Pinned = WeakTurn.Pin();
				return Pinned && (Pinned->StepCount > 0 || (Pinned->bActive && Session->IsBusy())) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			.AreaTitle_Lambda([this, WeakTurn]()
			{
				const TSharedPtr<FTurn> Pinned = WeakTurn.Pin();
				if (!Pinned)
				{
					return FText::GetEmpty();
				}
				FString Title = Pinned->ToolCount > 0 ? FString::Printf(TEXT("작업 과정 · 도구 %d회"), Pinned->ToolCount) : FString(TEXT("작업 과정"));
				if (Pinned->bActive && Session->IsBusy() && !Session->GetActivity().IsEmpty())
				{
					Title += TEXT(" · ") + Session->GetActivity();
				}
				return FText::FromString(Title);
			})
			.BodyContent()
			[
				SAssignNew(Turn->Steps, SVerticalBox)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(Turn->Answer, SVerticalBox)
		]
	];
	return Turn;
}

void SAgentMcpChatPanel::AddStep(FTurn& Turn, const TSharedRef<SWidget>& Widget)
{
	++Turn.StepCount;
	Turn.Steps->AddSlot()
	.AutoHeight()
	.Padding(8.0f, 1.0f)
	[
		Widget
	];
}

void SAgentMcpChatPanel::DemoteAnswer(FTurn& Turn)
{
	if (Turn.AnswerText.IsEmpty())
	{
		return;
	}
	AddStep(Turn, AgentMcpChatPanelPrivate::MakeMarkdownText(Turn.AnswerText));
	Turn.AnswerText.Reset();
	Turn.Answer->ClearChildren();
}

void SAgentMcpChatPanel::ScrollToEnd()
{
	MessageScroll->ScrollToEnd();
}

void SAgentMcpChatPanel::Submit()
{
	// A setup step waiting for the sign-in code takes the text instead of the chat.
	if (Session->IsAwaitingInput())
	{
		if (Session->SubmitInput(InputBox->GetText().ToString()))
		{
			InputBox->SetText(FText::GetEmpty());
		}
		return;
	}
	if (Session->IsBusy())
	{
		return;
	}
	if (Session->Send(InputBox->GetText().ToString()))
	{
		InputBox->SetText(FText::GetEmpty());
	}
}

FReply SAgentMcpChatPanel::OnSendClicked()
{
	if (Session->IsBusy())
	{
		Session->Cancel();
	}
	else
	{
		Submit();
	}
	return FReply::Handled();
}

FReply SAgentMcpChatPanel::OnNewChatClicked()
{
	StartNewConversation();
	return FReply::Handled();
}

FReply SAgentMcpChatPanel::OnSettingsClicked()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->ShowViewer("Editor", "Plugins", GetDefault<UAgentMcpChatSettings>()->GetSectionName());
	}
	return FReply::Handled();
}

FReply SAgentMcpChatPanel::OnInputKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (KeyEvent.GetKey() == EKeys::Enter && !KeyEvent.IsShiftDown())
	{
		Submit();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FText SAgentMcpChatPanel::GetProviderText() const
{
	return FText::FromString(GetProviderLabel(GetDefault<UAgentMcpChatSettings>()->Provider));
}

void SAgentMcpChatPanel::OnProviderSelected(TSharedPtr<EAgentMcpChatProvider> Item, ESelectInfo::Type SelectInfo)
{
	if (!Item.IsValid())
	{
		return;
	}
	UAgentMcpChatSettings* Settings = GetMutableDefault<UAgentMcpChatSettings>();
	if (Settings->Provider != *Item)
	{
		Settings->Provider = *Item;
		Settings->SaveConfig();
		RefreshProviderOptions();
		// Whether it is ready shows in the connection list, which opens by itself when it is not.
		Connections->Refresh(*Item);
	}
}


FText SAgentMcpChatPanel::GetModelText() const
{
	UAgentMcpChatSettings* Settings = GetMutableDefault<UAgentMcpChatSettings>();
	return FText::FromString(GetModelLabel(Settings->GetModel(Settings->Provider)));
}

FString SAgentMcpChatPanel::GetModelLabel(const FString& Id) const
{
	if (AgentMcpChatPanelPrivate::IsClaudeCode())
	{
		return GetChoiceLabel(GetClaudeCodeModelChoices(), Id);
	}
	if (AgentMcpChatPanelPrivate::IsAntigravity() || AgentMcpChatPanelPrivate::IsCodex())
	{
		return Id.IsEmpty() ? FString(TEXT("기본 (계정 설정)")) : Session->GetCliModelLabel(Id);
	}
	return Id.IsEmpty() ? FString(TEXT("모델 선택")) : Id;
}


void SAgentMcpChatPanel::OnModelListOpening()
{
	// The list is loaded when the provider is chosen; this only retries when that failed.
	if (!bModelsLoaded && !bModelsLoading)
	{
		LoadModels();
	}
}

void SAgentMcpChatPanel::LoadModels()
{
	// The combo box never opens on an empty list, so it holds the current model until the real list is in.
	UAgentMcpChatSettings* Settings = GetMutableDefault<UAgentMcpChatSettings>();
	ModelOptions.Reset();
	ModelOptions.Add(MakeShared<FString>(Settings->GetModel(Settings->Provider)));
	ModelCombo->RefreshOptions();

	bModelsLoaded = false;
	bModelsLoading = true;
	ModelListMessage = TEXT("모델 목록을 불러오는 중...");
	const EAgentMcpChatProvider RequestedFor = Settings->Provider;
	TWeakPtr<SAgentMcpChatPanel> WeakSelf = SharedThis(this);
	Session->FetchModels([WeakSelf, RequestedFor](const TArray<FString>& Models, const FString& Error)
	{
		const TSharedPtr<SAgentMcpChatPanel> This = WeakSelf.Pin();
		// A list for a provider the person has already left is dropped.
		if (!This || GetDefault<UAgentMcpChatSettings>()->Provider != RequestedFor)
		{
			return;
		}
		This->bModelsLoading = false;
		This->bModelsLoaded = Models.Num() > 0;
		if (Models.Num() > 0)
		{
			This->ModelOptions.Reset();
			for (const FString& Model : Models)
			{
				This->ModelOptions.Add(MakeShared<FString>(Model));
			}
		}
		This->ModelListMessage = !Error.IsEmpty() ? TEXT("모델 목록: ") + Error : FString();
		This->ModelCombo->RefreshOptions();
		// The levels of the models are known now.
		This->RefreshEffortOptions();
	});
}


void SAgentMcpChatPanel::OnModelPicked(TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo)
{
	if (!Item.IsValid() || SelectInfo == ESelectInfo::Direct)
	{
		return;
	}
	UAgentMcpChatSettings* Settings = GetMutableDefault<UAgentMcpChatSettings>();
	Settings->GetModel(Settings->Provider) = *Item;
	if (AgentMcpChatPanelPrivate::IsAntigravity() || AgentMcpChatPanelPrivate::IsCodex())
	{
		// Each model has its own levels; keep the chosen one when the new model has it, otherwise take a middle one.
		FString& Effort = AgentMcpChatPanelPrivate::GetEffortSetting(*Settings);
		const FChatModel* Picked = Session->FindCliModel(*Item);
		if (Picked && Picked->Levels.IsEmpty())
		{
			Effort.Reset();
		}
		else if (Picked && !Effort.IsEmpty() && !Picked->Levels.Contains(Effort))
		{
			Effort = Picked->Levels.Contains(TEXT("medium")) ? FString(TEXT("medium")) : Picked->Levels.Last();
		}
	}
	Settings->SaveConfig();
	RefreshEffortOptions();
}


FText SAgentMcpChatPanel::GetEffortText() const
{
	UAgentMcpChatSettings* Settings = GetMutableDefault<UAgentMcpChatSettings>();
	const FString& Effort = AgentMcpChatPanelPrivate::GetEffortSetting(*Settings);
	return FText::FromString(Effort.IsEmpty() ? FString(TEXT("추론: 기본")) : GetChoiceLabel(GetCurrentEffortChoices(), Effort));
}

void SAgentMcpChatPanel::OnEffortPicked(TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo)
{
	if (!Item.IsValid() || SelectInfo == ESelectInfo::Direct)
	{
		return;
	}
	UAgentMcpChatSettings* Settings = GetMutableDefault<UAgentMcpChatSettings>();
	FString& Effort = AgentMcpChatPanelPrivate::GetEffortSetting(*Settings);
	if (Effort != *Item)
	{
		Effort = *Item;
		Settings->SaveConfig();
	}
}

FText SAgentMcpChatPanel::GetSendText() const
{
	return FText::FromString(Session->IsBusy() ? TEXT("중지") : TEXT("보내기"));
}

FText SAgentMcpChatPanel::GetStatusText() const
{
	if (Session->IsBusy())
	{
		return FText::FromString(Session->GetActivity());
	}
	return FText::FromString(ModelListMessage);
}

namespace AgentMcpChatPanelPrivate
{
	constexpr int32 MaxApprovalChars = 3000;

	FString Clip(const FString& Text)
	{
		return Text.Len() > MaxApprovalChars ? Text.Left(MaxApprovalChars) + TEXT("\n... (생략)") : Text;
	}

	/** What the card shows: file edits as before and after, other calls as their arguments. */
	FString DescribeApproval(const UE::AgentMcp::FAgentMcpApprovalRequest& Request)
	{
		const TSharedPtr<FJsonObject>& Arguments = Request.Arguments;
		FString Path;
		if (Arguments.IsValid() && Request.ToolName == TEXT("source_replace"))
		{
			FString OldText;
			FString NewText;
			Arguments->TryGetStringField(TEXT("path"), Path);
			Arguments->TryGetStringField(TEXT("oldText"), OldText);
			Arguments->TryGetStringField(TEXT("newText"), NewText);
			return FString::Printf(TEXT("파일: %s\n\n--- 지금 내용\n%s\n\n+++ 바꿀 내용\n%s"), *Path, *Clip(OldText), *Clip(NewText));
		}
		if (Arguments.IsValid() && Request.ToolName == TEXT("source_write"))
		{
			FString Content;
			bool bOverwrite = false;
			Arguments->TryGetStringField(TEXT("path"), Path);
			Arguments->TryGetStringField(TEXT("content"), Content);
			Arguments->TryGetBoolField(TEXT("bOverwrite"), bOverwrite);
			return FString::Printf(TEXT("파일: %s (%s)\n\n%s"), *Path, bOverwrite ? TEXT("있으면 통째로 덮어씀") : TEXT("새 파일"), *Clip(Content));
		}

		FString Json;
		if (Arguments.IsValid())
		{
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
			FJsonSerializer::Serialize(Arguments.ToSharedRef(), Writer);
		}
		return Clip(Json);
	}
}

void SAgentMcpChatPanel::AddApprovalCard(const TSharedRef<FPendingApproval>& Approval)
{
	using namespace AgentMcpChatPanelPrivate;

	const UE::AgentMcp::FAgentMcpApprovalRequest& Request = Approval->Request;
	const bool bDestructive = Request.Access == TEXT("Destructive");
	const FString Title = FString::Printf(TEXT("승인 요청 · %s%s"), *Request.ToolName, bDestructive ? TEXT("  (삭제 또는 되돌리기 어려운 변경)") : TEXT(""));

	TSharedPtr<SHorizontalBox> Buttons;
	TSharedRef<SWidget> Card = MakeBubble(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Title))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.MaxDesiredHeight(320.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					MakePlainText(DescribeApproval(Request), ETextKind::Tool)
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SAssignNew(Buttons, SHorizontalBox)
		],
		bDestructive ? DestructiveColor : ApprovalColor);

	const TWeakPtr<SHorizontalBox> WeakButtons = Buttons;
	auto Decide = [Approval, WeakButtons](bool bApproved, bool bForQuestion)
	{
		if (bForQuestion)
		{
			FChatApprovals::Get().ApproveAllForQuestion();
		}
		Approval->Resolve(bApproved, bApproved ? FString() : TEXT("The user denied this change."));
		if (const TSharedPtr<SHorizontalBox> Row = WeakButtons.Pin())
		{
			Row->ClearChildren();
			Row->AddSlot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(bApproved ? (bForQuestion ? TEXT("승인함 (이번 질문 동안 모두 승인)") : TEXT("승인함")) : TEXT("거부함")))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];
		}
		return FReply::Handled();
	};

	Buttons->AddSlot()
	.AutoWidth()
	[
		SNew(SButton)
		.Text(FText::FromString(TEXT("승인")))
		.OnClicked_Lambda([Decide]() { return Decide(true, false); })
	];
	Buttons->AddSlot()
	.AutoWidth()
	.Padding(4.0f, 0.0f, 0.0f, 0.0f)
	[
		SNew(SButton)
		.Text(FText::FromString(TEXT("거부")))
		.OnClicked_Lambda([Decide]() { return Decide(false, false); })
	];
	if (!bDestructive)
	{
		Buttons->AddSlot()
		.AutoWidth()
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.Text(FText::FromString(TEXT("이번 질문 동안 모두 승인")))
			.ToolTipText(FText::FromString(TEXT("이 질문이 끝날 때까지 나머지 변경도 묻지 않고 승인합니다.")))
			.OnClicked_Lambda([Decide]() { return Decide(true, true); })
		];
	}

	if (CurrentTurn && CurrentTurn->bActive)
	{
		CurrentTurn->Answer->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 60.0f, 0.0f)
		[
			Card
		];
	}
	else
	{
		MessageList->AddSlot()
		.AutoHeight()
		.Padding(6.0f, 4.0f, 66.0f, 4.0f)
		[
			Card
		];
	}
	ScrollToEnd();
}

FText SAgentMcpChatPanel::GetModeText() const
{
	return FText::FromString(TEXT("권한: ") + GetModeLabel(GetDefault<UAgentMcpChatSettings>()->Mode));
}

void SAgentMcpChatPanel::OnModePicked(TSharedPtr<EAgentMcpChatMode> Item, ESelectInfo::Type SelectInfo)
{
	if (!Item.IsValid() || SelectInfo == ESelectInfo::Direct)
	{
		return;
	}
	UAgentMcpChatSettings* Settings = GetMutableDefault<UAgentMcpChatSettings>();
	if (Settings->Mode != *Item)
	{
		Settings->Mode = *Item;
		Settings->SaveConfig();
		AddNote(FString::Printf(TEXT("권한 모드를 '%s'(으)로 바꿨습니다. 다음 질문부터 적용되고, 바꾸는 작업은 매번 이 패널에서 승인을 받습니다."), *GetModeLabel(*Item)), false);
	}
}

TSharedRef<SWidget> SAgentMcpChatPanel::MakeHistoryPanel()
{
	return SNew(SBox)
		.WidthOverride(220.0f)
		.Visibility_Lambda([this]() { return bShowHistory ? EVisibility::Visible : EVisibility::Collapsed; })
		.Padding(FMargin(4.0f, 4.0f, 0.0f, 4.0f))
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(4.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2.0f, 2.0f, 2.0f, 6.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("대화 목록")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(ConversationList, SVerticalBox)
					]
				]
			]
		];
}

void SAgentMcpChatPanel::RefreshConversationList()
{
	using namespace AgentMcpChatPanelPrivate;

	ConversationList->ClearChildren();
	if (Conversations.IsEmpty())
	{
		ConversationList->AddSlot()
		.AutoHeight()
		.Padding(2.0f)
		[
			MakePlainText(TEXT("아직 저장된 대화가 없습니다."), ETextKind::Note)
		];
		return;
	}

	for (const TSharedRef<FChatConversation>& Item : Conversations)
	{
		const bool bCurrent = Conversation.IsValid() && Conversation->Id == Item->Id;
		const FString When = (Item->UpdatedAt + (FDateTime::Now() - FDateTime::UtcNow())).ToString(TEXT("%m/%d %H:%M"));
		const FString Detail = FString::Printf(TEXT("%s · %s"), *GetProviderLabel(Item->State.Provider), *When);
		const TWeakPtr<FChatConversation> WeakItem = Item;

		ConversationList->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(bCurrent ? UserColor : FLinearColor(0.12f, 0.12f, 0.12f))
			.Padding(0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ToolTipText(FText::FromString(Item->Title))
					.OnClicked_Lambda([this, WeakItem]()
					{
						if (const TSharedPtr<FChatConversation> Target = WeakItem.Pin())
						{
							SwitchToConversation(Target.ToSharedRef());
						}
						return FReply::Handled();
					})
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromString(Item->Title.IsEmpty() ? FString(TEXT("(제목 없음)")) : Item->Title))
							.Font(FCoreStyle::GetDefaultFontStyle(bCurrent ? "Bold" : "Regular", 9))
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromString(Detail))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						]
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ToolTipText(FText::FromString(TEXT("이 대화를 지웁니다.")))
					.Text(FText::FromString(TEXT("X")))
					.OnClicked_Lambda([this, WeakItem]()
					{
						if (const TSharedPtr<FChatConversation> Target = WeakItem.Pin())
						{
							DeleteConversation(Target.ToSharedRef());
						}
						return FReply::Handled();
					})
				]
			]
		];
	}
}

void SAgentMcpChatPanel::RecordEntry(const FChatEntry& Entry)
{
	if (bReplaying || !Conversation.IsValid())
	{
		return;
	}
	Conversation->Entries.Add(Entry);
	const bool bNew = !Conversations.ContainsByPredicate([this](const TSharedRef<FChatConversation>& Item) { return Item->Id == Conversation->Id; });
	if (Conversation->Title.IsEmpty() && Entry.Kind == EChatEntryKind::User)
	{
		FString FirstLine = Entry.Text;
		FirstLine.Split(TEXT("\n"), &FirstLine, nullptr);
		Conversation->Title = FirstLine.TrimStartAndEnd().Left(40);
	}
	// A conversation is listed from its first question; notes before that (setup progress) are not worth a file.
	if (bNew && Conversation->Title.IsEmpty())
	{
		Conversation->Entries.Pop();
		return;
	}
	SaveConversation();
	if (bNew)
	{
		Conversations.Insert(Conversation.ToSharedRef(), 0);
		RefreshConversationList();
	}
}

void SAgentMcpChatPanel::SaveConversation()
{
	if (!Conversation.IsValid() || Conversation->Entries.IsEmpty())
	{
		return;
	}
	Conversation->UpdatedAt = FDateTime::UtcNow();
	Conversation->State = Session->CaptureState();
	History::Save(*Conversation);
}

void SAgentMcpChatPanel::ClearMessages()
{
	CurrentTurn.Reset();
	MessageList->ClearChildren();
}

void SAgentMcpChatPanel::StartNewConversation()
{
	if (Session->IsBusy())
	{
		AddNote(TEXT("답변 중에는 새 대화를 시작할 수 없습니다. 먼저 중지하세요."), false);
		return;
	}
	SaveConversation();
	Session->Reset();
	Conversation = History::Create();
	ClearMessages();
	RefreshConversationList();
}

void SAgentMcpChatPanel::SwitchToConversation(const TSharedRef<FChatConversation>& Target)
{
	if (Conversation.IsValid() && Conversation->Id == Target->Id)
	{
		return;
	}
	if (Session->IsBusy())
	{
		AddNote(TEXT("답변 중에는 다른 대화로 바꿀 수 없습니다. 끝나거나 중지한 뒤에 바꾸세요."), false);
		return;
	}

	SaveConversation();
	Session->RestoreState(Target->State);
	Conversation = Target;
	RefreshProviderOptions();

	ClearMessages();
	bReplaying = true;
	for (const FChatEntry& Entry : Target->Entries)
	{
		AddEntry(Entry);
	}
	bReplaying = false;
	if (CurrentTurn)
	{
		CurrentTurn->bActive = false;
	}
	RefreshConversationList();
}

void SAgentMcpChatPanel::DeleteConversation(const TSharedRef<FChatConversation>& Target)
{
	const FText Question = FText::FromString(FString::Printf(TEXT("'%s' 대화를 지울까요? 되돌릴 수 없습니다."),
		Target->Title.IsEmpty() ? TEXT("(제목 없음)") : *Target->Title));
	if (FMessageDialog::Open(EAppMsgType::YesNo, Question) != EAppReturnType::Yes)
	{
		return;
	}
	const bool bCurrent = Conversation.IsValid() && Conversation->Id == Target->Id;
	if (bCurrent && Session->IsBusy())
	{
		AddNote(TEXT("답변 중인 대화는 지울 수 없습니다."), false);
		return;
	}
	History::Delete(Target->Id);
	Conversations.RemoveAll([&Target](const TSharedRef<FChatConversation>& Item) { return Item->Id == Target->Id; });
	if (bCurrent)
	{
		Session->Reset();
		Conversation = History::Create();
		ClearMessages();
	}
	RefreshConversationList();
}

void SAgentMcpChatPanel::RefreshProviderOptions()
{
	ModelListMessage.Reset();
	LoadModels();
	RefreshEffortOptions();
}


TArray<FChatChoice> SAgentMcpChatPanel::GetCurrentEffortChoices() const
{
	using namespace AgentMcpChatPanelPrivate;

	if (!IsAntigravity() && !IsCodex())
	{
		return GetEffortChoices();
	}
	// The levels of the chosen model; before its list is read, the usual ones of that CLI.
	UAgentMcpChatSettings* Settings = GetMutableDefault<UAgentMcpChatSettings>();
	const FChatModel* Model = Session->FindCliModel(Settings->GetModel(Settings->Provider));
	const TArray<FString> Fallback = IsCodex()
		? TArray<FString>{ TEXT("low"), TEXT("medium"), TEXT("high"), TEXT("xhigh") }
		: TArray<FString>{ TEXT("low"), TEXT("medium"), TEXT("high") };
	TArray<FChatChoice> Choices;
	if (IsCodex())
	{
		Choices.Add({ FString(), TEXT("추론: 기본") });
	}
	for (const FString& Level : Model ? Model->Levels : Fallback)
	{
		Choices.Add({ Level, TEXT("추론: ") + GetEffortLabel(Level) });
	}
	return Choices;
}


void SAgentMcpChatPanel::RefreshEffortOptions()
{
	EffortOptions.Reset();
	for (const FChatChoice& Choice : GetCurrentEffortChoices())
	{
		EffortOptions.Add(MakeShared<FString>(Choice.Id));
	}
	EffortCombo->RefreshOptions();
}

TSharedRef<SWidget> SAgentMcpChatPanel::MakeConnectionPanel()
{
	return SNew(SBorder)
		.Visibility_Lambda([this]() { return bShowConnections ? EVisibility::Visible : EVisibility::Collapsed; })
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("AI 서비스 계정")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("다시 확인")))
					.OnClicked_Lambda([this]()
					{
						Connections->RefreshAll();
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("고급 설정")))
					.ToolTipText(FText::FromString(TEXT("API 키, 실행 파일 위치, 제한값입니다(에디터 환경설정 > 플러그인 > AI 채팅).")))
					.OnClicked(this, &SAgentMcpChatPanel::OnSettingsClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("닫기")))
					.OnClicked_Lambda([this]()
					{
						bShowConnections = false;
						return FReply::Handled();
					})
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(ConnectionRows, SVerticalBox)
			]
		];
}

void SAgentMcpChatPanel::RebuildConnectionRows()
{
	if (!ConnectionRows.IsValid())
	{
		return;
	}
	ConnectionRows->ClearChildren();

	const EAgentMcpChatProvider Current = GetDefault<UAgentMcpChatSettings>()->Provider;
	for (EAgentMcpChatProvider Provider : { EAgentMcpChatProvider::ClaudeCode, EAgentMcpChatProvider::Codex, EAgentMcpChatProvider::Antigravity,
		EAgentMcpChatProvider::OpenAI, EAgentMcpChatProvider::Gemini })
	{
		const FConnectionStatus Status = Connections->Get(Provider);
		const bool bCli = Provider == EAgentMcpChatProvider::ClaudeCode || Provider == EAgentMcpChatProvider::Codex
			|| Provider == EAgentMcpChatProvider::Antigravity;

		// The current provider not being ready is the one thing worth opening the list for.
		if (Provider == Current && bCli && (Status.State == EChatConnectionState::NotInstalled || Status.State == EChatConnectionState::SignedOut))
		{
			bShowConnections = true;
		}

		FLinearColor StatusColor = FLinearColor(0.6f, 0.6f, 0.6f);
		if (Status.State == EChatConnectionState::SignedIn || Status.State == EChatConnectionState::KeyReady)
		{
			StatusColor = FLinearColor(0.35f, 0.8f, 0.4f);
		}
		else if (Status.State == EChatConnectionState::SignedOut || Status.State == EChatConnectionState::NotInstalled
			|| Status.State == EChatConnectionState::KeyMissing || Status.State == EChatConnectionState::Failed)
		{
			StatusColor = FLinearColor(0.95f, 0.65f, 0.3f);
		}
		const FString StatusText = Status.State == EChatConnectionState::Unknown ? FString(TEXT("확인 전")) : Status.Detail;

		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(170.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(GetProviderLabel(Provider) + (Provider == Current ? TEXT("  (사용 중)") : TEXT(""))))
					.Font(FCoreStyle::GetDefaultFontStyle(Provider == Current ? "Bold" : "Regular", 9))
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(StatusText))
				.ColorAndOpacity(FSlateColor(StatusColor))
			];

		auto AddButton = [&Row](const TCHAR* Label, const TCHAR* Tip, TFunction<FReply()> OnClick, TAttribute<bool> Enabled)
		{
			Row->AddSlot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(Label))
				.ToolTipText(FText::FromString(Tip))
				.IsEnabled(Enabled)
				.OnClicked_Lambda(MoveTemp(OnClick))
			];
		};
		const TAttribute<bool> NotBusy = TAttribute<bool>::CreateLambda([this]() { return !Session->IsBusy(); });

		if (bCli && Status.State != EChatConnectionState::Checking)
		{
			if (Status.State == EChatConnectionState::SignedIn)
			{
				if (FChatConnections::CanSignOut(Provider))
				{
					AddButton(TEXT("로그아웃"), TEXT("이 PC에서 이 계정의 로그인을 지웁니다."), [this, Provider]()
					{
						Connections->SignOut(Provider);
						return FReply::Handled();
					}, NotBusy);
				}
			}
			else
			{
				AddButton(Status.State == EChatConnectionState::NotInstalled ? TEXT("설치 후 로그인") : TEXT("로그인"),
					TEXT("CLI가 없으면 설치하고, 브라우저에서 로그인합니다. 터미널은 필요 없습니다."), [this, Provider]()
					{
						ConnectProvider(Provider);
						return FReply::Handled();
					}, NotBusy);
			}
		}
		else if (!bCli)
		{
			AddButton(Status.State == EChatConnectionState::KeyReady ? TEXT("키 바꾸기") : TEXT("키 설정"), TEXT("고급 설정에서 API 키를 입력합니다."), [this]()
			{
				return OnSettingsClicked();
			}, TAttribute<bool>(true));
		}

		ConnectionRows->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			Row
		];
	}
}

void SAgentMcpChatPanel::ConnectProvider(EAgentMcpChatProvider Provider)
{
	if (Session->IsBusy())
	{
		return;
	}
	// Connecting a provider means using it, so it becomes the chat's provider too.
	UAgentMcpChatSettings* Settings = GetMutableDefault<UAgentMcpChatSettings>();
	if (Settings->Provider != Provider)
	{
		Settings->Provider = Provider;
		Settings->SaveConfig();
		RefreshProviderOptions();
	}
	bSetupRunning = true;
	switch (Provider)
	{
	case EAgentMcpChatProvider::Antigravity: Session->SetUpAntigravity(); break;
	case EAgentMcpChatProvider::Codex: Session->SetUpCodex(); break;
	default: Session->SetUpClaudeCode(); break;
	}
	RebuildConnectionRows();
}
