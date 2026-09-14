#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Runtime/Launch/Resources/Version.h"

/** Engine version gate. Branches on the engine version belong in this module only. */
#define AGENTMCP_ENGINE_AT_LEAST(Major, Minor) \
	(ENGINE_MAJOR_VERSION > (Major) || (ENGINE_MAJOR_VERSION == (Major) && ENGINE_MINOR_VERSION >= (Minor)))

namespace UE::AgentMcp::Compat
{
	/** Serializes a JSON object as condensed text. */
	AGENTMCPCOMPAT_API FString JsonObjectToString(const TSharedRef<FJsonObject>& Object);

	/** Serializes a JSON object as condensed UTF-8 bytes. UE 5.5's JSON writer has no UTF-8 output path. */
	AGENTMCPCOMPAT_API TArray<uint8> JsonObjectToUtf8(const TSharedRef<FJsonObject>& Object);

	/** Decodes UTF-8 bytes (for example an HTTP request body) into a string. */
	AGENTMCPCOMPAT_API FString Utf8ToString(const TArray<uint8>& Bytes);
}
