#include "AgentMcpAsyncResult.h"

#include "AgentMcpJson.h"
#include "UObject/Package.h"

UAgentMcpAsyncResult* UAgentMcpAsyncResult::Create(float InTimeoutSeconds, FPollFunction&& Poll)
{
	UAgentMcpAsyncResult* Result = NewObject<UAgentMcpAsyncResult>(GetTransientPackage());
	Result->TimeoutSeconds = InTimeoutSeconds > 0.0f ? InTimeoutSeconds : 30.0f;
	Result->PollFunction = MoveTemp(Poll);
	return Result;
}

void UAgentMcpAsyncResult::Complete(const TSharedRef<FJsonObject>& InPayload)
{
	if (bFinished)
	{
		return;
	}
	Payload = InPayload;
	bFinished = true;
}

void UAgentMcpAsyncResult::CompleteWithStruct(const UStruct* Struct, const void* Data)
{
	Complete(UE::AgentMcp::StructToJson(Struct, Data));
}

void UAgentMcpAsyncResult::Fail(const FString& Code, const FString& Text, const FString& Hint)
{
	if (bFinished)
	{
		return;
	}
	ErrorCode = Code.IsEmpty() ? FString(TEXT("TOOL_ERROR")) : Code;
	ErrorText = Text;
	ErrorHint = Hint;
	bFinished = true;
}

bool UAgentMcpAsyncResult::Tick()
{
	if (!bFinished && PollFunction)
	{
		PollFunction(*this);
	}
	return bFinished;
}
