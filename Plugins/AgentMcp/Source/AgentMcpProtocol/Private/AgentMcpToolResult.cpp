#include "AgentMcpToolResult.h"

#include "AgentMcpCompat.h"
#include "Misc/Base64.h"

TSharedRef<FJsonObject> FAgentMcpToolResult::ToJson() const
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetArrayField(TEXT("content"), Content);
	if (bIsError)
	{
		Object->SetBoolField(TEXT("isError"), true);
	}
	return Object;
}

FAgentMcpToolResult& FAgentMcpToolResult::AddText(const FString& Text)
{
	TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
	Item->SetStringField(TEXT("type"), TEXT("text"));
	Item->SetStringField(TEXT("text"), Text);
	Content.Add(MakeShared<FJsonValueObject>(Item));
	return *this;
}

FAgentMcpToolResult& FAgentMcpToolResult::AddJson(const TSharedRef<FJsonObject>& Object)
{
	return AddText(UE::AgentMcp::Compat::JsonObjectToString(Object));
}

FAgentMcpToolResult& FAgentMcpToolResult::AddImage(const FString& MimeType, const TArray<uint8>& Bytes)
{
	TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
	Item->SetStringField(TEXT("type"), TEXT("image"));
	Item->SetStringField(TEXT("data"), FBase64::Encode(Bytes));
	Item->SetStringField(TEXT("mimeType"), MimeType);
	Content.Add(MakeShared<FJsonValueObject>(Item));
	return *this;
}

FAgentMcpToolResult FAgentMcpToolResult::MakeText(const FString& Text)
{
	FAgentMcpToolResult Result;
	Result.AddText(Text);
	return Result;
}

FAgentMcpToolResult FAgentMcpToolResult::MakeJson(const TSharedRef<FJsonObject>& Object)
{
	FAgentMcpToolResult Result;
	Result.AddJson(Object);
	return Result;
}

FAgentMcpToolResult FAgentMcpToolResult::MakeError(const FString& Code, const FString& Message, const FString& Hint)
{
	TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
	Error->SetStringField(TEXT("code"), Code);
	Error->SetStringField(TEXT("message"), Message);
	if (!Hint.IsEmpty())
	{
		Error->SetStringField(TEXT("hint"), Hint);
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetObjectField(TEXT("error"), Error);

	FAgentMcpToolResult Result = MakeJson(Root);
	Result.bIsError = true;
	return Result;
}
