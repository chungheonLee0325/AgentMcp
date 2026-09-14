#include "AgentMcpProtocol.h"

#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogAgentMcpProtocol);

IMPLEMENT_MODULE(FDefaultModuleImpl, AgentMcpProtocol)

namespace UE::AgentMcp
{
	const TArray<FString>& GetSupportedProtocolVersions()
	{
		static const TArray<FString> Versions =
		{
			TEXT("2025-11-25"),
			TEXT("2025-06-18"),
			TEXT("2025-03-26"),
			TEXT("2024-11-05"),
		};
		return Versions;
	}

	FString NegotiateProtocolVersion(const FString& RequestedVersion)
	{
		return GetSupportedProtocolVersions().Contains(RequestedVersion) ? RequestedVersion : FString(FallbackProtocolVersion);
	}

	bool IsValidMcpToolName(const FString& Name)
	{
		// MCP tool names: 1 to 128 ASCII letters, digits, underscores, hyphens and dots.
		constexpr int32 MaxLength = 128;
		if (Name.IsEmpty() || Name.Len() > MaxLength)
		{
			return false;
		}
		for (const TCHAR Character : Name)
		{
			const bool bAsciiLetterOrDigit = Character < 128 && FChar::IsAlnum(Character);
			if (!bAsciiLetterOrDigit && Character != TEXT('_') && Character != TEXT('-') && Character != TEXT('.'))
			{
				return false;
			}
		}
		return true;
	}
}
