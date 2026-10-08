#include "AgentMcpChatProviders.h"

#include "AgentMcpCompat.h"

#include "Dom/JsonValue.h"
#include "HAL/PlatformMisc.h"
#include "HttpModule.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UE::AgentMcp::Chat
{
	namespace ProvidersPrivate
	{
		TSharedPtr<FJsonObject> ParseObject(const FString& Text)
		{
			TSharedPtr<FJsonObject> Object;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
			if (!FJsonSerializer::Deserialize(Reader, Object))
			{
				return nullptr;
			}
			return Object;
		}

		/** The error message of a failed API call. OpenAI and Gemini answer with {"error": {"message": ...}}. */
		FString DescribeHttpError(int32 ResponseCode, const FString& Body)
		{
			FString Message;
			if (const TSharedPtr<FJsonObject> Object = ParseObject(Body))
			{
				const TSharedPtr<FJsonObject>* Error = nullptr;
				if (Object->TryGetObjectField(TEXT("error"), Error))
				{
					(*Error)->TryGetStringField(TEXT("message"), Message);
				}
			}
			if (Message.IsEmpty())
			{
				Message = Body.Left(500);
			}

			FString Hint;
			if (ResponseCode == 401 || ResponseCode == 403)
			{
				Hint = TEXT(" 설정에서 API 키를 확인하세요.");
			}
			else if (ResponseCode == 404)
			{
				Hint = TEXT(" 모델 이름을 확인하세요. 모델 목록에서 이 키로 쓸 수 있는 모델을 볼 수 있습니다.");
			}
			else if (ResponseCode == 429)
			{
				Hint = TEXT(" 이 키의 요청 한도나 사용량 한도에 도달했습니다.");
			}
			return FString::Printf(TEXT("HTTP %d: %s%s"), ResponseCode, *Message, *Hint);
		}

		FHttpRequestRef MakeRequest(const TCHAR* Verb, const FString& Url, float TimeoutSeconds)
		{
			FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
			Request->SetVerb(Verb);
			Request->SetURL(Url);
			Request->SetTimeout(TimeoutSeconds);
			return Request;
		}

		TSharedRef<FJsonObject> MakeTextMessage(const TCHAR* Role, const FString& Text)
		{
			TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
			Message->SetStringField(TEXT("role"), Role);
			Message->SetStringField(TEXT("content"), Text);
			return Message;
		}

		/**
		 * Copy of a JSON schema without the keywords Gemini's function declarations reject. Gemini accepts an OpenAPI subset, and
		 * additionalProperties fails the whole request.
		 */
		TSharedPtr<FJsonValue> SanitizeSchemaValueForGemini(const TSharedPtr<FJsonValue>& Value);

		TSharedRef<FJsonObject> SanitizeSchemaForGemini(const TSharedPtr<FJsonObject>& Schema)
		{
			static const TSet<FString> Unsupported = { TEXT("additionalProperties"), TEXT("uniqueItems"), TEXT("default"), TEXT("$schema") };

			TSharedRef<FJsonObject> Copy = MakeShared<FJsonObject>();
			if (!Schema.IsValid())
			{
				return Copy;
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Schema->Values)
			{
				if (!Unsupported.Contains(Field.Key))
				{
					Copy->SetField(Field.Key, SanitizeSchemaValueForGemini(Field.Value));
				}
			}
			return Copy;
		}

		TSharedPtr<FJsonValue> SanitizeSchemaValueForGemini(const TSharedPtr<FJsonValue>& Value)
		{
			if (!Value.IsValid())
			{
				return Value;
			}
			if (Value->Type == EJson::Object)
			{
				return MakeShared<FJsonValueObject>(SanitizeSchemaForGemini(Value->AsObject()));
			}
			if (Value->Type == EJson::Array)
			{
				TArray<TSharedPtr<FJsonValue>> Items;
				for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
				{
					Items.Add(SanitizeSchemaValueForGemini(Item));
				}
				return MakeShared<FJsonValueArray>(Items);
			}
			return Value;
		}

		TSharedRef<FJsonObject> SchemaOrEmpty(const TSharedPtr<FJsonObject>& Schema)
		{
			if (Schema.IsValid())
			{
				return Schema.ToSharedRef();
			}
			TSharedRef<FJsonObject> Empty = MakeShared<FJsonObject>();
			Empty->SetStringField(TEXT("type"), TEXT("object"));
			Empty->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
			return Empty;
		}

		TArray<FString> ReadModelIds(const FString& Body)
		{
			TArray<FString> Ids;
			const TSharedPtr<FJsonObject> Object = ParseObject(Body);
			const TArray<TSharedPtr<FJsonValue>>* Data = nullptr;
			if (Object.IsValid() && Object->TryGetArrayField(TEXT("data"), Data))
			{
				for (const TSharedPtr<FJsonValue>& Entry : *Data)
				{
					const TSharedPtr<FJsonObject>* EntryObject = nullptr;
					FString Id;
					if (Entry->TryGetObject(EntryObject) && (*EntryObject)->TryGetStringField(TEXT("id"), Id))
					{
						// Gemini's compatibility layer lists models/<name> and accepts the bare name.
						Id.RemoveFromStart(TEXT("models/"));
						Ids.Add(Id);
					}
				}
			}
			Ids.Sort();
			return Ids;
		}

		/** ChatGPT, Gemini and other servers through the OpenAI chat completions API. */
		class FOpenAiCompatibleProvider final : public IChatProvider
		{
		public:
			explicit FOpenAiCompatibleProvider(const FProviderConfig& InConfig) : Config(InConfig) {}

			virtual void SetConfig(const FProviderConfig& InConfig) override { Config = InConfig; }
			virtual const FProviderConfig& GetConfig() const override { return Config; }

			virtual void AddUserText(const FString& Text) override
			{
				History.Add(MakeShared<FJsonValueObject>(MakeTextMessage(TEXT("user"), Text)));
			}

			virtual void AddToolOutcomes(const TArray<FToolOutcome>& Outcomes) override
			{
				for (const FToolOutcome& Outcome : Outcomes)
				{
					TSharedRef<FJsonObject> Message = MakeTextMessage(TEXT("tool"), Outcome.bIsError ? TEXT("Error: ") + Outcome.Text : Outcome.Text);
					Message->SetStringField(TEXT("tool_call_id"), Outcome.CallId);
					History.Add(MakeShared<FJsonValueObject>(Message));
				}
			}

			virtual int32 GetHistoryLength() const override { return History.Num(); }
			virtual const TArray<TSharedPtr<FJsonValue>>& GetHistory() const override { return History; }
			virtual void SetHistory(const TArray<TSharedPtr<FJsonValue>>& InHistory) override { History = InHistory; }
			virtual void TruncateHistory(int32 Length) override { History.SetNum(FMath::Clamp(Length, 0, History.Num())); }

			virtual FHttpRequestRef MakeTurnRequest(const FString& SystemPrompt, const TArray<FToolSpec>& Tools) const override
			{
				TArray<TSharedPtr<FJsonValue>> Messages;
				Messages.Add(MakeShared<FJsonValueObject>(MakeTextMessage(TEXT("system"), SystemPrompt)));
				Messages.Append(History);

				TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
				Body->SetStringField(TEXT("model"), Config.Model);
				Body->SetArrayField(TEXT("messages"), Messages);

				const bool bGemini = Config.Provider == EAgentMcpChatProvider::Gemini;
				TArray<TSharedPtr<FJsonValue>> ToolArray;
				for (const FToolSpec& Tool : Tools)
				{
					TSharedRef<FJsonObject> Function = MakeShared<FJsonObject>();
					Function->SetStringField(TEXT("name"), Tool.Name);
					Function->SetStringField(TEXT("description"), Tool.Description);
					Function->SetObjectField(TEXT("parameters"), bGemini ? SanitizeSchemaForGemini(Tool.InputSchema) : SchemaOrEmpty(Tool.InputSchema));

					TSharedRef<FJsonObject> ToolObject = MakeShared<FJsonObject>();
					ToolObject->SetStringField(TEXT("type"), TEXT("function"));
					ToolObject->SetObjectField(TEXT("function"), Function);
					ToolArray.Add(MakeShared<FJsonValueObject>(ToolObject));
				}
				if (ToolArray.Num() > 0)
				{
					Body->SetArrayField(TEXT("tools"), ToolArray);
				}

				FHttpRequestRef Request = MakeRequest(TEXT("POST"), Config.BaseUrl + TEXT("/chat/completions"), Config.TimeoutSeconds);
				Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
				if (!Config.ApiKey.IsEmpty())
				{
					Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Config.ApiKey);
				}
				Request->SetContentAsString(UE::AgentMcp::Compat::JsonObjectToString(Body));
				return Request;
			}

			virtual FModelReply ParseTurnResponse(int32 ResponseCode, const FString& Body) override
			{
				FModelReply Reply;
				const TSharedPtr<FJsonObject> Object = ParseObject(Body);
				if (ResponseCode != 200 || !Object.IsValid())
				{
					Reply.Error = DescribeHttpError(ResponseCode, Body);
					return Reply;
				}

				const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
				const TSharedPtr<FJsonObject>* Choice = nullptr;
				const TSharedPtr<FJsonObject>* Message = nullptr;
				if (!Object->TryGetArrayField(TEXT("choices"), Choices) || Choices->Num() == 0 || !(*Choices)[0]->TryGetObject(Choice)
					|| !(*Choice)->TryGetObjectField(TEXT("message"), Message))
				{
					Reply.Error = TEXT("응답에 메시지가 없습니다.");
					return Reply;
				}

				(*Message)->TryGetStringField(TEXT("content"), Reply.Text);
				Reply.Text.TrimStartAndEndInline();

				const TArray<TSharedPtr<FJsonValue>>* ToolCalls = nullptr;
				if ((*Message)->TryGetArrayField(TEXT("tool_calls"), ToolCalls))
				{
					for (const TSharedPtr<FJsonValue>& CallValue : *ToolCalls)
					{
						const TSharedPtr<FJsonObject>* CallObject = nullptr;
						const TSharedPtr<FJsonObject>* Function = nullptr;
						if (!CallValue->TryGetObject(CallObject) || !(*CallObject)->TryGetObjectField(TEXT("function"), Function))
						{
							continue;
						}
						FToolCall Call;
						Call.Id = (*CallObject)->GetStringField(TEXT("id"));
						Call.Name = (*Function)->GetStringField(TEXT("name"));
						// Arguments arrive as a JSON string; an empty string means no arguments.
						FString ArgumentText;
						(*Function)->TryGetStringField(TEXT("arguments"), ArgumentText);
						Call.Arguments = ArgumentText.TrimStartAndEnd().IsEmpty() ? MakeShared<FJsonObject>() : ParseObject(ArgumentText);
						Reply.ToolCalls.Add(MoveTemp(Call));
					}
				}

				// Echoed unchanged: Gemini attaches thought signatures to its tool calls and expects them back.
				History.Add(MakeShared<FJsonValueObject>(*Message));

				FString FinishReason;
				(*Choice)->TryGetStringField(TEXT("finish_reason"), FinishReason);
				if (FinishReason == TEXT("content_filter"))
				{
					Reply.Note = TEXT("모델이 이 요청을 거절했습니다.");
				}
				else if (FinishReason == TEXT("length"))
				{
					Reply.Note = TEXT("출력 토큰 한도에 걸려 답변이 잘렸습니다.");
				}

				Reply.bOk = true;
				return Reply;
			}

			virtual FHttpRequestRef MakeModelListRequest() const override
			{
				FHttpRequestRef Request = MakeRequest(TEXT("GET"), Config.BaseUrl + TEXT("/models"), 30.0f);
				if (!Config.ApiKey.IsEmpty())
				{
					Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Config.ApiKey);
				}
				return Request;
			}

			virtual TArray<FString> ParseModelList(int32 ResponseCode, const FString& Body, FString& OutError) const override
			{
				if (ResponseCode != 200)
				{
					OutError = DescribeHttpError(ResponseCode, Body);
					return {};
				}

				// The OpenAI list also has image, audio and embedding models, which cannot chat.
				static const TCHAR* const NonChat[] = { TEXT("embedding"), TEXT("tts"), TEXT("whisper"), TEXT("dall-e"), TEXT("image"),
					TEXT("audio"), TEXT("realtime"), TEXT("transcribe"), TEXT("moderation"), TEXT("imagen"), TEXT("veo") };
				TArray<FString> Ids = ReadModelIds(Body);
				Ids.RemoveAll([](const FString& Id)
				{
					for (const TCHAR* Word : NonChat)
					{
						if (Id.Contains(Word))
						{
							return true;
						}
					}
					return false;
				});
				return Ids;
			}

		private:
			FProviderConfig Config;
			TArray<TSharedPtr<FJsonValue>> History;
		};
	}

	bool ReadProviderConfig(EAgentMcpChatProvider Provider, FProviderConfig& Out, FString& OutError)
	{
		UAgentMcpChatSettings* Settings = GetMutableDefault<UAgentMcpChatSettings>();

		Out = FProviderConfig();
		Out.Provider = Provider;
		Out.Model = Settings->GetModel(Provider).TrimStartAndEnd();
		Out.TimeoutSeconds = Settings->RequestTimeoutSeconds;

		const TCHAR* KeyVariable = nullptr;
		switch (Provider)
		{
		case EAgentMcpChatProvider::OpenAI:
			Out.BaseUrl = TEXT("https://api.openai.com/v1");
			Out.ApiKey = Settings->OpenAIApiKey;
			KeyVariable = TEXT("OPENAI_API_KEY");
			break;
		case EAgentMcpChatProvider::Gemini:
			Out.BaseUrl = TEXT("https://generativelanguage.googleapis.com/v1beta/openai");
			Out.ApiKey = Settings->GeminiApiKey;
			KeyVariable = TEXT("GEMINI_API_KEY");
			break;
		case EAgentMcpChatProvider::OpenAICompatible:
			Out.BaseUrl = Settings->CustomBaseUrl.TrimStartAndEnd();
			Out.ApiKey = Settings->CustomApiKey;
			break;
		case EAgentMcpChatProvider::ClaudeCode:
			// The CLI brings its own login and default model, so neither a key nor a model is required.
			Out.Effort = Settings->ClaudeCodeEffort.TrimStartAndEnd();
			return true;
		case EAgentMcpChatProvider::Antigravity:
			Out.Effort = Settings->AntigravityEffort.TrimStartAndEnd();
			return true;
		case EAgentMcpChatProvider::Codex:
			Out.Effort = Settings->CodexEffort.TrimStartAndEnd();
			return true;
		}

		Out.ApiKey.TrimStartAndEndInline();
		if (Out.ApiKey.IsEmpty() && KeyVariable)
		{
			Out.ApiKey = FPlatformMisc::GetEnvironmentVariable(KeyVariable).TrimStartAndEnd();
		}
		while (Out.BaseUrl.EndsWith(TEXT("/")))
		{
			Out.BaseUrl.LeftChopInline(1);
		}

		if (Out.BaseUrl.IsEmpty())
		{
			OutError = TEXT("설정에서 호환 서버의 기본 URL을 입력하세요.");
			return false;
		}
		// A local OpenAI-compatible server often needs no key.
		if (Out.ApiKey.IsEmpty() && KeyVariable)
		{
			OutError = FString::Printf(TEXT("%s API 키가 없습니다. 설정(에디터 환경설정 > 플러그인 > Agent MCP Chat)에 입력하거나 환경 변수 %s를 설정하세요."),
				*GetProviderLabel(Provider), KeyVariable);
			return false;
		}
		if (Out.Model.IsEmpty())
		{
			OutError = TEXT("먼저 모델을 고르세요. 모델 목록에서 이 키로 쓸 수 있는 모델을 볼 수 있습니다.");
			return false;
		}
		return true;
	}

	FString GetProviderLabel(EAgentMcpChatProvider Provider)
	{
		switch (Provider)
		{
		case EAgentMcpChatProvider::OpenAI: return TEXT("ChatGPT API");
		case EAgentMcpChatProvider::Gemini: return TEXT("Gemini API");
		case EAgentMcpChatProvider::OpenAICompatible: return TEXT("호환 서버");
		case EAgentMcpChatProvider::ClaudeCode: return TEXT("Claude Code");
		case EAgentMcpChatProvider::Antigravity: return TEXT("Gemini (Antigravity)");
		case EAgentMcpChatProvider::Codex: return TEXT("ChatGPT (Codex)");
		}
		return TEXT("모델");
	}

	TUniquePtr<IChatProvider> MakeProvider(const FProviderConfig& Config)
	{
		return MakeUnique<ProvidersPrivate::FOpenAiCompatibleProvider>(Config);
	}
}

FString& UAgentMcpChatSettings::GetModel(EAgentMcpChatProvider InProvider)
{
	switch (InProvider)
	{
	case EAgentMcpChatProvider::OpenAI: return OpenAIModel;
	case EAgentMcpChatProvider::Gemini: return GeminiModel;
	case EAgentMcpChatProvider::OpenAICompatible: return CustomModel;
	case EAgentMcpChatProvider::Antigravity: return AntigravityModel;
	case EAgentMcpChatProvider::Codex: return CodexModel;
	default: return ClaudeCodeModel;
	}
}
