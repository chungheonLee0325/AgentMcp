#include "AgentMcpErrorScope.h"

#include "AgentMcpToolset.h"
#include "AgentMcpToolsetLog.h"

namespace UE::AgentMcp
{
	namespace ErrorScopePrivate
	{
		thread_local FToolErrorScope* CurrentToolErrorScope = nullptr;
	}

	FToolErrorScope::FToolErrorScope()
		: Previous(ErrorScopePrivate::CurrentToolErrorScope)
	{
		ErrorScopePrivate::CurrentToolErrorScope = this;
	}

	FToolErrorScope::~FToolErrorScope()
	{
		ErrorScopePrivate::CurrentToolErrorScope = Previous;
	}

	void FToolErrorScope::SetError(const FString& InCode, const FString& InText, const FString& InHint)
	{
		if (bHasError)
		{
			return;
		}
		bHasError = true;
		ErrorCode = InCode;
		ErrorText = InText;
		ErrorHint = InHint;
	}

	FToolErrorScope* FToolErrorScope::GetCurrent()
	{
		return ErrorScopePrivate::CurrentToolErrorScope;
	}

	void RaiseToolError(const FString& Code, const FString& Message, const FString& Hint)
	{
		if (FToolErrorScope* Scope = FToolErrorScope::GetCurrent())
		{
			Scope->SetError(Code, Message, Hint);
		}
		else
		{
			UE_LOG(LogAgentMcpToolset, Warning, TEXT("RaiseToolError called outside a tool call: [%s] %s"), *Code, *Message);
		}
	}

	bool HasToolError()
	{
		const FToolErrorScope* Scope = FToolErrorScope::GetCurrent();
		return Scope && Scope->HasError();
	}
}
