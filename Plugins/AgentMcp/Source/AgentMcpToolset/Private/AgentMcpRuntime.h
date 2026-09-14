#pragma once

#include "CoreMinimal.h"
#include "AgentMcpToolset.h"
#include "Templates/UniquePtr.h"

class FAgentMcpServer;

namespace UE::AgentMcp
{
	class FReflectedTool;
	struct FToolCatalog;

	/** Owns the tool registry and the MCP server for the editor session. Game thread only. */
	class FAgentMcpRuntime
	{
	public:
		static FAgentMcpRuntime& Get();

		FAgentMcpRuntime();
		~FAgentMcpRuntime();

		/** Scans toolsets and starts the server when bAutoStartServer is set. */
		void Startup();
		void Shutdown();

		/** Rebuilds the tool list (after hot reload or Live Coding) and updates the server. */
		void Rescan();

		bool RestartServer(FString& OutError);

		FAgentMcpRuntimeInfo GetInfo() const;

	private:
		void BuildTools();
		void ApplyToServer();

		TUniquePtr<FAgentMcpServer> Server;
		TArray<TSharedRef<FReflectedTool>> AllTools;
		TSharedPtr<FToolCatalog> Catalog;
		FString LastError;
		bool bStarted = false;
	};
}
