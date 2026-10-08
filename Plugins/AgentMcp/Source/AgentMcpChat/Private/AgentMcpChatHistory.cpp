#include "AgentMcpChatHistory.h"

#include "AgentMcpChatTypes.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UE::AgentMcp::Chat::History
{
	namespace HistoryPrivate
	{
		FString GetFolder()
		{
			return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AgentMcpChat"), TEXT("Conversations")));
		}

		FString GetPath(const FString& Id)
		{
			return FPaths::Combine(GetFolder(), Id + TEXT(".json"));
		}

		const TCHAR* KindToString(EChatEntryKind Kind)
		{
			switch (Kind)
			{
			case EChatEntryKind::User: return TEXT("user");
			case EChatEntryKind::Assistant: return TEXT("assistant");
			case EChatEntryKind::Tool: return TEXT("tool");
			case EChatEntryKind::Error: return TEXT("error");
			default: return TEXT("status");
			}
		}

		EChatEntryKind KindFromString(const FString& Text)
		{
			if (Text == TEXT("user")) return EChatEntryKind::User;
			if (Text == TEXT("assistant")) return EChatEntryKind::Assistant;
			if (Text == TEXT("tool")) return EChatEntryKind::Tool;
			if (Text == TEXT("error")) return EChatEntryKind::Error;
			return EChatEntryKind::Status;
		}

		TSharedPtr<FChatConversation> FromJson(const TSharedPtr<FJsonObject>& Object)
		{
			if (!Object.IsValid())
			{
				return nullptr;
			}
			TSharedRef<FChatConversation> Conversation = MakeShared<FChatConversation>();
			if (!Object->TryGetStringField(TEXT("id"), Conversation->Id) || Conversation->Id.IsEmpty())
			{
				return nullptr;
			}
			Object->TryGetStringField(TEXT("title"), Conversation->Title);
			FString Time;
			if (Object->TryGetStringField(TEXT("createdAt"), Time))
			{
				FDateTime::ParseIso8601(*Time, Conversation->CreatedAt);
			}
			if (Object->TryGetStringField(TEXT("updatedAt"), Time))
			{
				FDateTime::ParseIso8601(*Time, Conversation->UpdatedAt);
			}

			FString ProviderName;
			if (Object->TryGetStringField(TEXT("provider"), ProviderName))
			{
				const int64 Value = StaticEnum<EAgentMcpChatProvider>()->GetValueByNameString(ProviderName);
				if (Value != INDEX_NONE)
				{
					Conversation->State.Provider = static_cast<EAgentMcpChatProvider>(Value);
				}
			}
			Object->TryGetStringField(TEXT("claudeCodeSessionId"), Conversation->State.ClaudeCodeSessionId);
			Object->TryGetStringField(TEXT("antigravityConversationId"), Conversation->State.AntigravityConversationId);
			Object->TryGetStringField(TEXT("codexThreadId"), Conversation->State.CodexThreadId);
			const TArray<TSharedPtr<FJsonValue>>* ApiHistory = nullptr;
			if (Object->TryGetArrayField(TEXT("apiHistory"), ApiHistory))
			{
				Conversation->State.ApiHistory = *ApiHistory;
			}

			const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
			if (Object->TryGetArrayField(TEXT("entries"), Entries))
			{
				for (const TSharedPtr<FJsonValue>& Value : *Entries)
				{
					const TSharedPtr<FJsonObject>* EntryObject = nullptr;
					if (!Value->TryGetObject(EntryObject))
					{
						continue;
					}
					FChatEntry& Entry = Conversation->Entries.AddDefaulted_GetRef();
					FString Kind;
					(*EntryObject)->TryGetStringField(TEXT("kind"), Kind);
					Entry.Kind = KindFromString(Kind);
					(*EntryObject)->TryGetStringField(TEXT("speaker"), Entry.Speaker);
					(*EntryObject)->TryGetStringField(TEXT("text"), Entry.Text);
				}
			}
			return Conversation;
		}
	}

	TSharedRef<FChatConversation> Create()
	{
		TSharedRef<FChatConversation> Conversation = MakeShared<FChatConversation>();
		Conversation->Id = FGuid::NewGuid().ToString(EGuidFormats::DigitsLower);
		Conversation->CreatedAt = FDateTime::UtcNow();
		Conversation->UpdatedAt = Conversation->CreatedAt;
		return Conversation;
	}

	TArray<TSharedRef<FChatConversation>> LoadAll()
	{
		using namespace HistoryPrivate;

		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *FPaths::Combine(GetFolder(), TEXT("*.json")), /*Files=*/true, /*Directories=*/false);

		TArray<TSharedRef<FChatConversation>> Conversations;
		for (const FString& File : Files)
		{
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(GetFolder(), File)))
			{
				continue;
			}
			TSharedPtr<FJsonObject> Object;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
			if (FJsonSerializer::Deserialize(Reader, Object))
			{
				if (const TSharedPtr<FChatConversation> Conversation = FromJson(Object))
				{
					Conversations.Add(Conversation.ToSharedRef());
				}
			}
		}
		Conversations.Sort([](const TSharedRef<FChatConversation>& A, const TSharedRef<FChatConversation>& B) { return A->UpdatedAt > B->UpdatedAt; });
		return Conversations;
	}

	bool Save(const FChatConversation& Conversation)
	{
		using namespace HistoryPrivate;

		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("id"), Conversation.Id);
		Object->SetStringField(TEXT("title"), Conversation.Title);
		Object->SetStringField(TEXT("createdAt"), Conversation.CreatedAt.ToIso8601());
		Object->SetStringField(TEXT("updatedAt"), Conversation.UpdatedAt.ToIso8601());
		Object->SetStringField(TEXT("provider"), StaticEnum<EAgentMcpChatProvider>()->GetNameStringByValue(static_cast<int64>(Conversation.State.Provider)));
		Object->SetStringField(TEXT("claudeCodeSessionId"), Conversation.State.ClaudeCodeSessionId);
		Object->SetStringField(TEXT("antigravityConversationId"), Conversation.State.AntigravityConversationId);
		Object->SetStringField(TEXT("codexThreadId"), Conversation.State.CodexThreadId);
		Object->SetArrayField(TEXT("apiHistory"), Conversation.State.ApiHistory);

		TArray<TSharedPtr<FJsonValue>> Entries;
		for (const FChatEntry& Entry : Conversation.Entries)
		{
			TSharedRef<FJsonObject> EntryObject = MakeShared<FJsonObject>();
			EntryObject->SetStringField(TEXT("kind"), KindToString(Entry.Kind));
			EntryObject->SetStringField(TEXT("speaker"), Entry.Speaker);
			EntryObject->SetStringField(TEXT("text"), Entry.Text);
			Entries.Add(MakeShared<FJsonValueObject>(EntryObject));
		}
		Object->SetArrayField(TEXT("entries"), Entries);

		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Object, Writer);
		IFileManager::Get().MakeDirectory(*GetFolder(), /*Tree=*/true);
		const bool bSaved = FFileHelper::SaveStringToFile(Text, *GetPath(Conversation.Id), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		if (!bSaved)
		{
			UE_LOG(LogAgentMcpChat, Warning, TEXT("Could not save the conversation to %s."), *GetPath(Conversation.Id));
		}
		return bSaved;
	}

	void Delete(const FString& Id)
	{
		IFileManager::Get().Delete(*HistoryPrivate::GetPath(Id), /*RequireExists=*/false, /*EvenReadOnly=*/true);
	}
}
