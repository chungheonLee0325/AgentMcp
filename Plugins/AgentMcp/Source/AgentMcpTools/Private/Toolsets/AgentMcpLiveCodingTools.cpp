#include "AgentMcpLiveCodingTools.h"

#include "AgentMcpLogBuffer.h"

#include "HAL/PlatformTime.h"
#include "Modules/ModuleManager.h"

#if WITH_LIVE_CODING
#include "ILiveCodingModule.h"
#endif

namespace UE::AgentMcp::LiveCodingToolsPrivate
{
#if WITH_LIVE_CODING
	/** Names of ELiveCodingCompileResult values, in declaration order (ILiveCodingModule.h). */
	constexpr const TCHAR* CompileResultNames[] =
	{
		TEXT("Success"), TEXT("NoChanges"), TEXT("InProgress"), TEXT("CompileStillActive"), TEXT("NotStarted"), TEXT("Failure"), TEXT("Cancelled"),
	};

	FString GetCompileResultName(ELiveCodingCompileResult CompileResult)
	{
		const int32 Index = static_cast<int32>(CompileResult);
		const int32 NameCount = static_cast<int32>(UE_ARRAY_COUNT(CompileResultNames));
		return Index < NameCount ? FString(CompileResultNames[Index]) : FString::Printf(TEXT("Unknown(%d)"), Index);
	}

	void RaiseNotAvailable(const ILiveCodingModule& LiveCoding)
	{
		const FText& Reason = LiveCoding.GetEnableErrorText();
		UE::AgentMcp::RaiseToolError(TEXT("NOT_AVAILABLE"),
			Reason.IsEmpty()
				? FString(TEXT("Live Coding could not be started for this editor session."))
				: FString::Printf(TEXT("Live Coding could not be started: %s"), *Reason.ToString()),
			TEXT("Enable Live Coding in Editor Preferences, or close the editor and build with Build.bat."));
	}
#endif
}

FAgentMcpLiveCodingResult UAgentMcpLiveCodingTools::Compile()
{
	FAgentMcpLiveCodingResult Result;

#if WITH_LIVE_CODING
	using namespace UE::AgentMcp::LiveCodingToolsPrivate;

	ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
	if (!LiveCoding)
	{
		UE::AgentMcp::RaiseToolError(TEXT("NOT_AVAILABLE"), TEXT("The Live Coding module is not loaded."),
			TEXT("Close the editor and build with Build.bat instead."));
		return Result;
	}
	if (!LiveCoding->IsEnabledForSession() && !LiveCoding->CanEnableForSession())
	{
		RaiseNotAvailable(*LiveCoding);
		return Result;
	}

	Result.StartLogSequence = static_cast<int64>(FAgentMcpLogBuffer::Get().GetLatestSequence());
	const double CompileStart = FPlatformTime::Seconds();
	// Compile starts Live Coding for this session when needed (FLiveCodingModule::Compile). With WaitForCompletion it blocks the
	// game thread until the Live Coding console has finished, so no other request runs in the meantime.
	ELiveCodingCompileResult CompileResult = ELiveCodingCompileResult::NotStarted;
	LiveCoding->Compile(ELiveCodingCompileFlags::WaitForCompletion, &CompileResult);
	Result.DurationSeconds = FPlatformTime::Seconds() - CompileStart;
	if (CompileResult == ELiveCodingCompileResult::NotStarted)
	{
		RaiseNotAvailable(*LiveCoding);
		return Result;
	}
	Result.Result = GetCompileResultName(CompileResult);
#else
	UE::AgentMcp::RaiseToolError(TEXT("NOT_AVAILABLE"), TEXT("Live Coding is not available in this build configuration."),
		TEXT("Close the editor and build with Build.bat instead."));
#endif

	return Result;
}
