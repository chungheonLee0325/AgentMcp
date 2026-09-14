#include "AgentMcpLogBuffer.h"
#include "AgentMcpRuntime.h"
#include "AgentMcpSettings.h"
#include "AgentMcpToolsetLog.h"

#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY(LogAgentMcpToolset);

class FAgentMcpToolsetModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		if (!GIsEditor || IsRunningCommandlet())
		{
			return;
		}

		// Capture log lines from startup onward so log_get_recent can report editor load issues.
		FAgentMcpLogBuffer::Get().Register(GetDefault<UAgentMcpSettings>()->LogBufferLines);

		if (GEngine && GIsRunning)
		{
			OnPostEngineInit();
		}
		else
		{
			PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FAgentMcpToolsetModule::OnPostEngineInit);
		}

		ReloadCompleteHandle = FCoreUObjectDelegates::ReloadCompleteDelegate.AddRaw(this, &FAgentMcpToolsetModule::OnReloadComplete);
	}

	virtual void ShutdownModule() override
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
		FCoreUObjectDelegates::ReloadCompleteDelegate.Remove(ReloadCompleteHandle);

		UE::AgentMcp::FAgentMcpRuntime::Get().Shutdown();
		FAgentMcpLogBuffer::Get().Unregister();
	}

private:
	void OnPostEngineInit()
	{
		UE::AgentMcp::FAgentMcpRuntime::Get().Startup();
	}

	void OnReloadComplete(EReloadCompleteReason Reason)
	{
		UE::AgentMcp::FAgentMcpRuntime::Get().Rescan();
	}

	FDelegateHandle PostEngineInitHandle;
	FDelegateHandle ReloadCompleteHandle;
};

IMPLEMENT_MODULE(FAgentMcpToolsetModule, AgentMcpToolset)
