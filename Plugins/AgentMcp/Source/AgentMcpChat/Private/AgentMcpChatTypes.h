#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAgentMcpChat, Log, All);

namespace UE::AgentMcp::Chat
{
	/** A tool the model may call, as tools/list described it. */
	struct FToolSpec
	{
		FString Name;
		FString Description;
		TSharedPtr<FJsonObject> InputSchema;
		bool bReadOnly = true;
	};

	struct FToolCall
	{
		FString Id;
		FString Name;
		/** Null when the model sent arguments that are not a JSON object. */
		TSharedPtr<FJsonObject> Arguments;
	};

	struct FToolOutcome
	{
		FString CallId;
		FString Name;
		FString Text;
		bool bIsError = false;
	};

	/** One model response, already appended to the provider's history. */
	struct FModelReply
	{
		bool bOk = false;
		FString Error;
		FString Text;
		TArray<FToolCall> ToolCalls;
		/** Set when the answer ended early, for example when the model declined or ran out of output tokens. */
		FString Note;
	};
}
