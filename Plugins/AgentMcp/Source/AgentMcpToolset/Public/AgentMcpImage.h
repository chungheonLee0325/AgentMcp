#pragma once

#include "CoreMinimal.h"

#include "AgentMcpImage.generated.h"

/**
 * Image returned by a tool. Put it in the result struct of a tool function: the dispatcher
 * sends Data as MCP image content and replaces the field in the JSON text with a summary (mimeType, width, height, bytes).
 */
USTRUCT(BlueprintType)
struct AGENTMCPTOOLSET_API FAgentMcpImage
{
	GENERATED_BODY()

	/** For example image/png. */
	UPROPERTY()
	FString MimeType;

	/** Encoded image bytes. */
	UPROPERTY()
	TArray<uint8> Data;

	UPROPERTY()
	int32 Width = 0;

	UPROPERTY()
	int32 Height = 0;
};
