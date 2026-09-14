#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Templates/Function.h"
#include "UObject/Object.h"

#include "AgentMcpAsyncResult.generated.h"

/**
 * Result of a tool that finishes on a later frame, such as starting or stopping a play session.
 *
 * A tool creates it with Create, passing a poll function, and returns it. The dispatcher keeps the object alive and
 * ticks it on the game thread until the poll function calls Complete or Fail, the timeout expires or the client cancels.
 */
UCLASS(BlueprintType)
class AGENTMCPTOOLSET_API UAgentMcpAsyncResult : public UObject
{
	GENERATED_BODY()

public:
	using FPollFunction = TFunction<void(UAgentMcpAsyncResult& Result)>;

	/** Creates a pending result. The poll function runs every frame until it calls Complete or Fail. */
	static UAgentMcpAsyncResult* Create(float TimeoutSeconds, FPollFunction&& Poll);

	void Complete(const TSharedRef<FJsonObject>& InPayload);
	void CompleteWithStruct(const UStruct* Struct, const void* Data);

	template <typename StructType>
	void CompleteWith(const StructType& Value)
	{
		CompleteWithStruct(StructType::StaticStruct(), &Value);
	}

	void Fail(const FString& Code, const FString& Text, const FString& Hint = FString());

	/** Runs the poll function while pending. Returns true once finished. */
	bool Tick();

	bool IsFinished() const { return bFinished; }
	bool HasFailed() const { return bFinished && !ErrorCode.IsEmpty(); }
	float GetTimeoutSeconds() const { return TimeoutSeconds; }
	const TSharedPtr<FJsonObject>& GetPayload() const { return Payload; }
	const FString& GetErrorCode() const { return ErrorCode; }
	const FString& GetErrorText() const { return ErrorText; }
	const FString& GetErrorHint() const { return ErrorHint; }

private:
	FPollFunction PollFunction;
	TSharedPtr<FJsonObject> Payload;
	FString ErrorCode;
	FString ErrorText;
	FString ErrorHint;
	float TimeoutSeconds = 30.0f;
	bool bFinished = false;
};
