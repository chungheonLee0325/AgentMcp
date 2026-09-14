#pragma once

#include "CoreMinimal.h"
#include "IAgentMcpTool.h"

namespace UE::AgentMcp
{
	class FReflectedTool;

	/** Tools and toolset descriptions visible to clients after filtering. */
	struct FToolCatalog
	{
		TArray<TSharedRef<FReflectedTool>> Tools;
		TMap<FString, FString> ToolsetDescriptions;
	};

	/**
	 * ToolSearch exposure mode: toolsets_list, toolsets_describe and tools_call over the given catalog, for clients that
	 * cannot defer tool schemas.
	 */
	TArray<TSharedRef<IAgentMcpTool>> CreateToolSearchTools(const TSharedRef<const FToolCatalog>& Catalog);
}
