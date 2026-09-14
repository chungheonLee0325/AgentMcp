#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class FProperty;
class UStruct;

/**
 * JSON conversion helpers with the conventions of tool arguments and results:
 * strict type checks, object references exchanged as object paths (actor labels accepted for actors and components).
 */
namespace UE::AgentMcp
{
	/** True when values of the property can be exchanged as JSON (not delegates, interfaces or maps with non-text keys). */
	AGENTMCPTOOLSET_API bool IsJsonCompatibleProperty(const FProperty* Property);

	/** Converts a property value to JSON. Returns null for properties that are not JSON compatible. */
	AGENTMCPTOOLSET_API TSharedPtr<FJsonValue> PropertyValueToJson(const FProperty* Property, const void* ValuePtr);

	/** JSON Schema of a property value, with the same rules as tool input schemas. */
	AGENTMCPTOOLSET_API TSharedRef<FJsonObject> PropertyToJsonSchema(const FProperty* Property);

	/** Converts a struct value to a JSON object. */
	AGENTMCPTOOLSET_API TSharedRef<FJsonObject> StructToJson(const UStruct* Struct, const void* Data);

	/**
	 * Checks JsonValue against the property type, then converts it into ValuePtr (an initialized value of the property).
	 * Struct fields missing from the JSON keep their current value. On failure returns false with OutErrorCode
	 * (INVALID_ARGUMENT, NOT_FOUND or AMBIGUOUS_REFERENCE) and OutError; DisplayName names the value in messages.
	 */
	AGENTMCPTOOLSET_API bool JsonToPropertyValue(const TSharedPtr<FJsonValue>& JsonValue, FProperty* Property, void* ValuePtr, const FString& DisplayName, FString& OutErrorCode, FString& OutError);
}
