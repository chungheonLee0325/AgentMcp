#include "AgentMcpRuntime.h"

#include "AgentMcpMetaTools.h"
#include "AgentMcpReflectedTool.h"
#include "AgentMcpServer.h"
#include "AgentMcpSettings.h"
#include "AgentMcpToolsetLog.h"
#include "IAgentMcpTool.h"

#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "UObject/Class.h"
#include "UObject/UObjectHash.h"

namespace UE::AgentMcp::RuntimePrivate
{
	FString GetToolsetName(const UClass* Class)
	{
		FString Name = Class->GetMetaData(TEXT("McpToolset"));
		if (!Name.IsEmpty())
		{
			return Name;
		}

		Name = Class->GetName();
		Name.RemoveFromStart(TEXT("AgentMcp"));
		if (!Name.RemoveFromEnd(TEXT("Toolset")))
		{
			Name.RemoveFromEnd(TEXT("Tools"));
		}
		return ToSnakeCase(Name);
	}

	bool MatchesAnyPattern(const FString& Name, const TArray<FString>& Patterns)
	{
		for (const FString& Pattern : Patterns)
		{
			if (!Pattern.IsEmpty() && Name.MatchesWildcard(Pattern, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	FString BuildInstructions()
	{
		return FString::Printf(TEXT(
			"Unreal Editor tools for project '%s' (engine %s).\n"
			"- Start with editor_get_state: current level, play session, dirty packages, selection, undo state.\n"
			"- Read tools are side-effect free. Write tools run inside an undoable editor transaction (editor_undo reverts the last one), "
			"are blocked during a play session, and never save; asset_save saves explicitly. Destructive tools are dry runs unless bConfirm is true.\n"
			"- Tool failures are isError results with {\"error\":{\"code\",\"message\",\"hint\"}}.\n"
			"- log_get_recent returns nextSequence; pass it back as sinceSequence to read only newer lines.\n"
			"- Verify a change: blueprint_compile changed Blueprints, pie_start, log_get_recent from its startLogSequence, viewport_capture (shows the game UI), pie_stop.\n"
			"- After C++ changes call livecoding_compile. Changes to UCLASS, USTRUCT, UPROPERTY or UFUNCTION declarations need a full build with the editor closed.\n"
			"- Object arguments are object paths such as /Game/Folder/Asset.Asset; package names, actor labels and ActorLabel.ComponentName are also accepted.\n"
			"- List tools accept limit and cursor; request only the properties you need. Empty fields are left out of results."),
			FApp::GetProjectName(), *FEngineVersion::Current().ToString(EVersionComponent::Patch));
	}
}

namespace UE::AgentMcp
{
	FAgentMcpRuntime& FAgentMcpRuntime::Get()
	{
		static FAgentMcpRuntime Instance;
		return Instance;
	}

	FAgentMcpRuntime::FAgentMcpRuntime() = default;
	FAgentMcpRuntime::~FAgentMcpRuntime() = default;

	void FAgentMcpRuntime::BuildTools()
	{
		using namespace RuntimePrivate;

		AllTools.Reset();
		TMap<FString, FString> ToolsetDescriptions;

		TArray<UClass*> Classes;
		GetDerivedClasses(UAgentMcpToolset::StaticClass(), Classes, /*bRecursive=*/true);
		Classes.Sort([](const UClass& A, const UClass& B) { return A.GetName() < B.GetName(); });

		TSet<FString> ToolNames;
		for (UClass* Class : Classes)
		{
			if (!Class || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				continue;
			}
			const FString ClassName = Class->GetName();
			if (ClassName.StartsWith(TEXT("SKEL_")) || ClassName.StartsWith(TEXT("REINST_")))
			{
				continue;
			}

			const FString ToolsetName = GetToolsetName(Class);
			ToolsetDescriptions.FindOrAdd(ToolsetName) = Class->GetMetaData(TEXT("ToolTip"));

			for (TFieldIterator<UFunction> FunctionIt(Class, EFieldIteratorFlags::ExcludeSuper); FunctionIt; ++FunctionIt)
			{
				UFunction* Function = *FunctionIt;
				if (!Function->HasMetaData(TEXT("AICallable")) || Function->HasMetaData(TEXT("AIIgnore")))
				{
					continue;
				}

				FString Error;
				FString Warning;
				const TSharedPtr<FReflectedTool> Tool = FReflectedTool::Create(Class, Function, ToolsetName, Error, Warning);
				if (!Tool.IsValid())
				{
					UE_LOG(LogAgentMcpToolset, Error, TEXT("%s::%s %s; it is not exposed as a tool."), *ClassName, *Function->GetName(), *Error);
					continue;
				}
				if (!Warning.IsEmpty())
				{
					UE_LOG(LogAgentMcpToolset, Warning, TEXT("%s::%s %s."), *ClassName, *Function->GetName(), *Warning);
				}
				if (ToolNames.Contains(Tool->GetName()))
				{
					UE_LOG(LogAgentMcpToolset, Error, TEXT("%s::%s duplicates tool name '%s'; skipped."), *ClassName, *Function->GetName(), *Tool->GetName());
					continue;
				}

				ToolNames.Add(Tool->GetName());
				AllTools.Add(Tool.ToSharedRef());
			}
		}

		const UAgentMcpSettings* Settings = GetDefault<UAgentMcpSettings>();
		TSharedRef<FToolCatalog> NewCatalog = MakeShared<FToolCatalog>();
		for (const TSharedRef<FReflectedTool>& Tool : AllTools)
		{
			if (MatchesAnyPattern(Tool->GetName(), Settings->BlockedTools))
			{
				continue;
			}
			if (Settings->AllowedTools.Num() > 0 && !MatchesAnyPattern(Tool->GetName(), Settings->AllowedTools))
			{
				continue;
			}
			NewCatalog->Tools.Add(Tool);
		}
		NewCatalog->ToolsetDescriptions = MoveTemp(ToolsetDescriptions);
		Catalog = NewCatalog;
	}

	void FAgentMcpRuntime::ApplyToServer()
	{
		if (!Server || !Catalog.IsValid())
		{
			return;
		}

		TArray<TSharedRef<IAgentMcpTool>> ExposedTools;
		if (GetDefault<UAgentMcpSettings>()->ExposureMode == EAgentMcpExposureMode::ToolSearch)
		{
			ExposedTools = CreateToolSearchTools(Catalog.ToSharedRef());
		}
		else
		{
			for (const TSharedRef<FReflectedTool>& Tool : Catalog->Tools)
			{
				ExposedTools.Add(Tool);
			}
		}
		Server->SetTools(ExposedTools);
	}

	void FAgentMcpRuntime::Startup()
	{
		if (bStarted)
		{
			return;
		}
		bStarted = true;

		BuildTools();
		UE_LOG(LogAgentMcpToolset, Log, TEXT("Agent MCP registered %d tools (%d after filters)."), AllTools.Num(), Catalog.IsValid() ? Catalog->Tools.Num() : 0);

		if (!GetDefault<UAgentMcpSettings>()->bAutoStartServer)
		{
			UE_LOG(LogAgentMcpToolset, Log, TEXT("Agent MCP server auto-start is disabled (Project Settings > Plugins > Agent MCP)."));
			return;
		}

		FString Error;
		RestartServer(Error);
	}

	bool FAgentMcpRuntime::RestartServer(FString& OutError)
	{
		const UAgentMcpSettings* Settings = GetDefault<UAgentMcpSettings>();
		if (!Server)
		{
			Server = MakeUnique<FAgentMcpServer>();
		}
		Server->Stop();

		FAgentMcpServerConfig Config;
		Config.Port = static_cast<uint32>(FMath::Clamp(Settings->Port, 1024, 65535));
		Config.UrlPath = Settings->UrlPath.IsEmpty() ? FString(TEXT("/mcp")) : Settings->UrlPath;
		Config.AuthToken = Settings->AuthToken;
		Config.Instructions = RuntimePrivate::BuildInstructions();

		ApplyToServer();
		if (!Server->Start(Config, OutError))
		{
			LastError = OutError;
			return false;
		}
		LastError.Reset();
		return true;
	}

	void FAgentMcpRuntime::Rescan()
	{
		if (!bStarted)
		{
			return;
		}
		BuildTools();
		ApplyToServer();
		UE_LOG(LogAgentMcpToolset, Log, TEXT("Agent MCP rescanned toolsets: %d tools (%d after filters)."), AllTools.Num(), Catalog.IsValid() ? Catalog->Tools.Num() : 0);
	}

	void FAgentMcpRuntime::Shutdown()
	{
		if (Server)
		{
			Server->Stop();
			Server.Reset();
		}
		Catalog.Reset();
		AllTools.Reset();
		bStarted = false;
	}

	FAgentMcpRuntimeInfo FAgentMcpRuntime::GetInfo() const
	{
		FAgentMcpRuntimeInfo Info;
		Info.bServerRunning = Server && Server->IsRunning();
		Info.EndpointUrl = Server ? Server->GetEndpointUrl() : FString();
		Info.RegisteredToolCount = AllTools.Num();
		Info.ExposedToolCount = Server ? Server->GetToolCount() : 0;
		Info.ExposureMode = StaticEnum<EAgentMcpExposureMode>()->GetNameStringByValue(static_cast<int64>(GetDefault<UAgentMcpSettings>()->ExposureMode));
		Info.LastError = LastError;
		return Info;
	}

	FAgentMcpRuntimeInfo GetRuntimeInfo()
	{
		return FAgentMcpRuntime::Get().GetInfo();
	}
}
