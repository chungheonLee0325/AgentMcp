#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/**
 * Result of an MCP tools/call.
 * @see https://modelcontextprotocol.io/specification/2025-06-18/server/tools#tool-result
 */
struct AGENTMCPPROTOCOL_API FAgentMcpToolResult
{
	/** MCP content items (text, image). */
	TArray<TSharedPtr<FJsonValue>> Content;

	/** Tool execution failure. Protocol failures are JSON-RPC errors instead. */
	bool bIsError = false;

	TSharedRef<FJsonObject> ToJson() const;

	FAgentMcpToolResult& AddText(const FString& Text);
	/** Adds the object as condensed JSON text. */
	FAgentMcpToolResult& AddJson(const TSharedRef<FJsonObject>& Object);
	/** Adds a base64 image content item. */
	FAgentMcpToolResult& AddImage(const FString& MimeType, const TArray<uint8>& Bytes);

	static FAgentMcpToolResult MakeText(const FString& Text);
	static FAgentMcpToolResult MakeJson(const TSharedRef<FJsonObject>& Object);

	/** isError=true with {"error":{"code","message","hint"}}. */
	static FAgentMcpToolResult MakeError(const FString& Code, const FString& Message, const FString& Hint = FString());
};
