#include "AgentMcpCompat.h"

#include "Containers/StringConv.h"
#include "Modules/ModuleManager.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, AgentMcpCompat)

namespace UE::AgentMcp::Compat
{
	FString JsonObjectToString(const TSharedRef<FJsonObject>& Object)
	{
		FString Output;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(Object, Writer);
		return Output;
	}

	TArray<uint8> JsonObjectToUtf8(const TSharedRef<FJsonObject>& Object)
	{
		const FString Text = JsonObjectToString(Object);
		const FTCHARToUTF8 Converted(*Text, Text.Len());

		TArray<uint8> Bytes;
		Bytes.Append(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
		return Bytes;
	}

	FString Utf8ToString(const TArray<uint8>& Bytes)
	{
		if (Bytes.Num() == 0)
		{
			return FString();
		}

		const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
		return FString(Converted.Length(), Converted.Get());
	}
}
