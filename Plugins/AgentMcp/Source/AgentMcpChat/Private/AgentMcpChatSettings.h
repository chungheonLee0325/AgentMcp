#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "AgentMcpChatSettings.generated.h"

UENUM()
enum class EAgentMcpChatProvider : uint8
{
	/** The Claude Code CLI with the user's own Claude login, so no API key is needed. */
	ClaudeCode UMETA(DisplayName = "Claude Code (로그인)"),
	/** The Antigravity CLI (agy), Google's successor of the Gemini CLI, with the user's own Google login. */
	Antigravity UMETA(DisplayName = "Gemini (Antigravity 로그인)"),
	/** The Codex CLI with the user's own ChatGPT login. */
	Codex UMETA(DisplayName = "ChatGPT (Codex 로그인)"),
	OpenAI UMETA(DisplayName = "ChatGPT (OpenAI API)"),
	Gemini UMETA(DisplayName = "Gemini (Google API)"),
	/** Any server that speaks the OpenAI chat completions API, for example a local Ollama. */
	OpenAICompatible UMETA(DisplayName = "OpenAI 호환 서버"),
};

/** What the chat's model may change. Every change still waits for the person's approval in the panel. */
UENUM()
enum class EAgentMcpChatMode : uint8
{
	ReadOnly UMETA(DisplayName = "읽기 전용"),
	/** Levels, actors, Blueprints, widgets, DataTables, string tables and other assets, through the editor tools. */
	AssetEdit UMETA(DisplayName = "에셋 편집"),
	/** Text files in the source folders (C++, config) and Live Coding. */
	CodeEdit UMETA(DisplayName = "코드 편집"),
	AssetAndCodeEdit UMETA(DisplayName = "에셋 + 코드 편집"),
};

/**
 * Per-user chat settings. They live in the user's EditorSettings.ini, not in a project file, so API keys are never committed.
 * The models and reasoning levels are picked in the chat panel and stored here without being shown on the settings page.
 */
UCLASS(Config = EditorSettings, meta = (DisplayName = "AI 채팅"))
class UAgentMcpChatSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetContainerName() const override { return TEXT("Editor"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
#if WITH_EDITOR
	virtual FText GetSectionText() const override { return FText::FromString(TEXT("AI 채팅")); }
	virtual FText GetSectionDescription() const override
	{
		return FText::FromString(TEXT("AI 채팅 패널의 설정입니다. 이 PC의 사용자 설정에만 저장되고 프로젝트에는 들어가지 않습니다. ")
			TEXT("모델과 추론 깊이는 채팅 패널 위쪽에서 고릅니다."));
	}
#endif

	UPROPERTY(Config, EditAnywhere, Category = "기본", meta = (DisplayName = "공급자", ToolTip = "채팅에 쓸 AI 서비스입니다. 채팅 패널에서도 바꿀 수 있습니다."))
	EAgentMcpChatProvider Provider = EAgentMcpChatProvider::ClaudeCode;

	UPROPERTY(Config, EditAnywhere, Category = "기본", meta = (DisplayName = "권한 모드", ToolTip = "모델이 무엇을 바꿀 수 있는지 정합니다. 어떤 모드든 바꾸기 전에 패널에서 승인을 받습니다."))
	EAgentMcpChatMode Mode = EAgentMcpChatMode::ReadOnly;

	/** claude.exe. Empty searches .local/bin in the user folder, where the native installer puts it, and then PATH. */
	UPROPERTY(Config, EditAnywhere, Category = "실행 파일 (비워 두면 자동으로 찾음)", meta = (DisplayName = "Claude Code", ToolTip = "claude.exe 위치입니다. 비워 두면 사용자 폴더의 .local/bin과 PATH에서 찾습니다."))
	FString ClaudeCodePath;

	/** agy.exe. Empty searches %LOCALAPPDATA%/agy/bin, where the official installer puts it, and then PATH. */
	UPROPERTY(Config, EditAnywhere, Category = "실행 파일 (비워 두면 자동으로 찾음)", meta = (DisplayName = "Gemini (Antigravity)", ToolTip = "agy.exe 위치입니다. 비워 두면 LOCALAPPDATA의 agy/bin과 PATH에서 찾습니다."))
	FString AntigravityPath;

	/** codex.exe. Empty searches %LOCALAPPDATA%/Programs/OpenAI/Codex/bin, where the official installer puts it, and then PATH. */
	UPROPERTY(Config, EditAnywhere, Category = "실행 파일 (비워 두면 자동으로 찾음)", meta = (DisplayName = "ChatGPT (Codex)", ToolTip = "codex.exe 위치입니다. 비워 두면 LOCALAPPDATA의 Programs/OpenAI/Codex/bin과 PATH에서 찾습니다."))
	FString CodexPath;

	/** Falls back to the OPENAI_API_KEY environment variable. */
	UPROPERTY(Config, EditAnywhere, Category = "API 키 (API 공급자만)", meta = (PasswordField = true, DisplayName = "OpenAI", ToolTip = "ChatGPT (OpenAI API) 공급자에 씁니다. 비워 두면 OPENAI_API_KEY 환경 변수를 씁니다."))
	FString OpenAIApiKey;

	/** Falls back to the GEMINI_API_KEY environment variable. */
	UPROPERTY(Config, EditAnywhere, Category = "API 키 (API 공급자만)", meta = (PasswordField = true, DisplayName = "Gemini", ToolTip = "Gemini (Google API) 공급자에 씁니다. 비워 두면 GEMINI_API_KEY 환경 변수를 씁니다."))
	FString GeminiApiKey;

	/** Base URL that /chat/completions and /models are appended to, for example http://localhost:11434/v1. */
	UPROPERTY(Config, EditAnywhere, Category = "OpenAI 호환 서버", meta = (DisplayName = "기본 URL", ToolTip = "/chat/completions와 /models를 붙일 주소입니다. 예: http://localhost:11434/v1"))
	FString CustomBaseUrl;

	/** Sent as a bearer token when set. */
	UPROPERTY(Config, EditAnywhere, Category = "OpenAI 호환 서버", meta = (PasswordField = true, DisplayName = "API 키", ToolTip = "입력하면 Bearer 토큰으로 보냅니다. 키가 필요 없는 서버는 비워 두세요."))
	FString CustomApiKey;

	/** Shown here because a server without a model list needs it typed in. */
	UPROPERTY(Config, EditAnywhere, Category = "OpenAI 호환 서버", meta = (DisplayName = "모델", ToolTip = "서버가 모델 목록을 주지 않으면 여기에 모델 이름을 적습니다."))
	FString CustomModel;

	/** Model requests in one answer, each followed by the tool calls it asked for. The answer stops with an error past this. */
	UPROPERTY(Config, EditAnywhere, Category = "제한", meta = (ClampMin = "1", ClampMax = "100", DisplayName = "최대 도구 라운드 (API 공급자만)", ToolTip = "답변 하나에 모델에게 요청하는 최대 횟수입니다. 넘으면 오류로 멈춥니다. 로그인 공급자(CLI)는 CLI가 정합니다."))
	int32 MaxToolRounds = 16;

	UPROPERTY(Config, EditAnywhere, Category = "제한", meta = (ClampMin = "10.0", DisplayName = "요청 제한 시간(초)", ToolTip = "API 공급자의 응답을 기다리는 최대 시간입니다."))
	float RequestTimeoutSeconds = 300.0f;

	// Picked in the chat panel; stored with the other settings but not shown on the settings page.

	/** A full model name such as claude-opus-5-5. Empty uses the account default. */
	UPROPERTY(Config)
	FString ClaudeCodeModel;

	/** low, medium, high, xhigh or max. Empty uses the model default. */
	UPROPERTY(Config)
	FString ClaudeCodeEffort;

	/** A model from agy models: the base id (gemini-3.8-flash) when it has levels, otherwise the listed id. */
	UPROPERTY(Config)
	FString AntigravityModel;

	/** low, medium or high, among the levels of the model. */
	UPROPERTY(Config)
	FString AntigravityEffort;

	/** A model name such as gpt-5.5. Empty uses the Codex default. */
	UPROPERTY(Config)
	FString CodexModel;

	/** low, medium, high or xhigh. Empty uses the model default. */
	UPROPERTY(Config)
	FString CodexEffort;

	UPROPERTY(Config)
	FString OpenAIModel = TEXT("gpt-5.5");

	UPROPERTY(Config)
	FString GeminiModel = TEXT("gemini-3-flash-preview");

	/** The model name stored for a provider. */
	FString& GetModel(EAgentMcpChatProvider InProvider);
};
