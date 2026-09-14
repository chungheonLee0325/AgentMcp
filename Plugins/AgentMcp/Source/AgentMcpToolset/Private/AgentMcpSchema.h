#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FProperty;
class UFunction;
class UStruct;

/**
 * JSON Schema generation from reflection data, limited to the property types FJsonObjectConverter round-trips, so that a
 * schema always describes the JSON the invoker accepts and returns.
 */
namespace UE::AgentMcp::Schema
{
	/** JSON field name of a property (FJsonObjectConverter::StandardizeCase), used for inputs, outputs and nested structs. */
	FString GetJsonName(const FProperty* Property);

	/** Parameter supplied by the caller: not the return value and not a non-const reference. */
	bool IsInputParameter(const FProperty* Property);

	/** Parameter written back to the caller: the return value or a non-const reference. */
	bool IsOutputParameter(const FProperty* Property);

	/** False (with a reason) when the property type cannot be exchanged as JSON. */
	bool IsSupportedProperty(const FProperty* Property, FString& OutReason);

	TSharedRef<FJsonObject> MakePropertySchema(const FProperty* Property, int32 Depth = 0);
	TSharedRef<FJsonObject> MakeStructSchema(const UStruct* Struct, int32 Depth = 0);

	/** "type":"object" schema of a tool function's input parameters, with @param docs as descriptions. */
	TSharedRef<FJsonObject> MakeInputSchema(const UFunction* Function);

	/** Tool description from the function comment (summary plus @return). */
	FString MakeToolDescription(const UFunction* Function);

	/** Default value text from UHT CPP_Default_ metadata. False means the parameter is required. */
	bool TryGetParameterDefault(const UFunction* Function, const FProperty* Parameter, FString& OutDefault);

	/** True when the parameter is listed in AutoCreateRefTerm metadata: optional, and default-constructed when omitted. */
	bool IsAutoCreateRefTerm(const UFunction* Function, const FProperty* Parameter);
}
