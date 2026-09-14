#pragma once

#include "CoreMinimal.h"
#include "AgentMcpAsyncResult.h"
#include "AgentMcpImage.h"
#include "AgentMcpJson.h"
#include "Dom/JsonObject.h"
#include "UObject/StrongObjectPtr.h"

class UClass;
class UFunction;

namespace UE::AgentMcp
{
	struct FInvokeOutcome
	{
		bool bSuccess = false;

		/** Tool output: a struct return value becomes the object itself, other values use "returnValue"; out parameters are merged in. */
		TSharedPtr<FJsonObject> Result;

		/** Set when the tool returned a UAgentMcpAsyncResult; the dispatcher waits for it to finish. */
		TStrongObjectPtr<UAgentMcpAsyncResult> AsyncResult;

		/** FAgentMcpImage values of the result, sent as MCP image content. */
		TArray<FAgentMcpImage> Images;

		FString ErrorCode;
		FString ErrorText;
		FString ErrorHint;
	};

	/**
	 * Removes empty strings, empty lists and empty nested objects from a result built from the return struct of Function, so
	 * results do not repeat fields that carry no information. Values inside FJsonObjectWrapper fields (property and row values)
	 * are data and stay untouched.
	 */
	void CompactToolResult(const UFunction* Function, FJsonObject& Result);

	/** True when the function returns a UAgentMcpAsyncResult and finishes on a later frame. */
	bool IsAsyncToolFunction(const UFunction* Function);

	/**
	 * Converts Arguments into the function's parameters, calls the static tool function through the class default object
	 * and converts the outputs back to JSON. Object references are exchanged as object paths. Game thread only.
	 */
	FInvokeOutcome InvokeToolFunction(UClass* ToolsetClass, UFunction* Function, const TSharedRef<FJsonObject>& Arguments);
}
