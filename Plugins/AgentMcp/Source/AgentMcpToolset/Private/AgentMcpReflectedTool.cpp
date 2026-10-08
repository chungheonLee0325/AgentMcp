#include "AgentMcpReflectedTool.h"

#include "AgentMcpAsyncResult.h"
#include "AgentMcpCompat.h"
#include "AgentMcpInvoker.h"
#include "AgentMcpRuntime.h"
#include "AgentMcpSchema.h"
#include "AgentMcpSettings.h"
#include "AgentMcpToolsetLog.h"

#include "Containers/Ticker.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "HAL/PlatformTime.h"
#include "ScopedTransaction.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

namespace UE::AgentMcp::ReflectedToolPrivate
{
	constexpr int32 MaxToolNameLength = 64;

	/** Extra seconds the dispatcher waits beyond the timeout of an asynchronous result before failing the call. */
	constexpr double AsyncTimeoutGraceSeconds = 5.0;

	bool IsEditorBusy()
	{
		return GIsSavingPackage || IsGarbageCollecting() || IsAsyncLoading();
	}

	int32 GetUndoableTransactionCount()
	{
		return (GEditor && GEditor->Trans) ? GEditor->Trans->GetQueueLength() - GEditor->Trans->GetUndoCount() : 0;
	}

	bool ParseAccess(const FString& Text, EToolAccess& OutAccess)
	{
		if (Text.Equals(TEXT("Read"), ESearchCase::IgnoreCase)) { OutAccess = EToolAccess::Read; return true; }
		if (Text.Equals(TEXT("Write"), ESearchCase::IgnoreCase)) { OutAccess = EToolAccess::Write; return true; }
		if (Text.Equals(TEXT("Destructive"), ESearchCase::IgnoreCase)) { OutAccess = EToolAccess::Destructive; return true; }
		if (Text.Equals(TEXT("Control"), ESearchCase::IgnoreCase)) { OutAccess = EToolAccess::Control; return true; }
		return false;
	}

	bool IsValidAgentToolName(const FString& Name)
	{
		if (Name.IsEmpty() || Name.Len() > MaxToolNameLength)
		{
			return false;
		}
		for (const TCHAR Character : Name)
		{
			if (!((Character >= TEXT('a') && Character <= TEXT('z')) || (Character >= TEXT('0') && Character <= TEXT('9')) || Character == TEXT('_')))
			{
				return false;
			}
		}
		return true;
	}

	const TCHAR* AccessNote(EToolAccess Access)
	{
		switch (Access)
		{
		case EToolAccess::Read:
			return TEXT("[Read-only]");
		case EToolAccess::Write:
			return TEXT("[Write: recorded as an undoable editor transaction (editor_undo); blocked during PIE; does not save]");
		case EToolAccess::Destructive:
			return TEXT("[Destructive: recorded as an editor transaction where supported; blocked during PIE; does not save]");
		case EToolAccess::Control:
		default:
			return TEXT("[Editor control]");
		}
	}

	/** Characters one field of a result takes when written on its own. */
	int32 FieldLength(const FString& Name, const TSharedPtr<FJsonValue>& Value)
	{
		const TSharedRef<FJsonObject> Single = MakeShared<FJsonObject>();
		Single->SetField(Name, Value);
		return UE::AgentMcp::Compat::JsonObjectToString(Single).Len();
	}

	FAgentMcpToolResult MakeTextResult(const TSharedRef<FJsonObject>& ResultObject)
	{
		FString Text = UE::AgentMcp::Compat::JsonObjectToString(ResultObject);
		const int32 MaxCharacters = FMath::Max(1024, GetDefault<UAgentMcpSettings>()->MaxResultBytes);
		if (Text.Len() <= MaxCharacters)
		{
			return FAgentMcpToolResult::MakeText(Text);
		}

		// Clients read the result as JSON, and a write tool's change has already happened when its result is too long, so the
		// result is cut at item boundaries, never mid-text: the longest top-level arrays lose items from the end, then the
		// largest remaining fields are left out. "truncated" says what went.
		const TSharedRef<FJsonObject> Trimmed = MakeShared<FJsonObject>(*ResultObject);
		const TSharedRef<FJsonObject> Truncated = MakeShared<FJsonObject>();
		Truncated->SetNumberField(TEXT("characters"), Text.Len());
		Truncated->SetStringField(TEXT("hint"), TEXT("Narrow the request with limit, cursor or propertyNames."));
		const FString TruncatedField = TEXT("truncated");
		for (;;)
		{
			Trimmed->SetObjectField(TruncatedField, Truncated);
			Text = UE::AgentMcp::Compat::JsonObjectToString(Trimmed);
			if (Text.Len() <= MaxCharacters)
			{
				break;
			}
			FString Longest;
			int32 LongestItems = 1;
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Trimmed->Values)
			{
				if (Pair.Key != TruncatedField && Pair.Value.IsValid() && Pair.Value->Type == EJson::Array && Pair.Value->AsArray().Num() > LongestItems)
				{
					Longest = Pair.Key;
					LongestItems = Pair.Value->AsArray().Num();
				}
			}
			if (!Longest.IsEmpty())
			{
				TArray<TSharedPtr<FJsonValue>> Items = Trimmed->GetArrayField(Longest);
				const TSharedPtr<FJsonObject>* Earlier = nullptr;
				const int32 Total = Truncated->TryGetObjectField(Longest, Earlier) ? static_cast<int32>((*Earlier)->GetNumberField(TEXT("total"))) : Items.Num();
				Items.SetNum(Items.Num() / 2);
				Trimmed->SetArrayField(Longest, Items);
				const TSharedRef<FJsonObject> Kept = MakeShared<FJsonObject>();
				Kept->SetNumberField(TEXT("total"), Total);
				Kept->SetNumberField(TEXT("kept"), Items.Num());
				Truncated->SetObjectField(Longest, Kept);
				continue;
			}
			// No array has more than one item left: the largest other field goes whole.
			FString Largest;
			int32 LargestLength = 0;
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Trimmed->Values)
			{
				if (Pair.Key != TruncatedField && Pair.Value.IsValid())
				{
					const int32 Length = FieldLength(Pair.Key, Pair.Value);
					if (Length > LargestLength)
					{
						Largest = Pair.Key;
						LargestLength = Length;
					}
				}
			}
			if (Largest.IsEmpty())
			{
				break;
			}
			Trimmed->RemoveField(Largest);
			Truncated->SetStringField(Largest, TEXT("omitted"));
		}
		return FAgentMcpToolResult::MakeText(Text);
	}

	FAgentMcpToolResult MakeAsyncToolResult(const UAgentMcpAsyncResult& Result)
	{
		if (Result.HasFailed())
		{
			return FAgentMcpToolResult::MakeError(Result.GetErrorCode(), Result.GetErrorText(), Result.GetErrorHint());
		}
		const TSharedPtr<FJsonObject>& Payload = Result.GetPayload();
		return MakeTextResult(Payload.IsValid() ? Payload.ToSharedRef() : MakeShared<FJsonObject>());
	}
}

namespace UE::AgentMcp
{
	const TCHAR* LexToString(EToolAccess Access)
	{
		switch (Access)
		{
		case EToolAccess::Read: return TEXT("Read");
		case EToolAccess::Write: return TEXT("Write");
		case EToolAccess::Destructive: return TEXT("Destructive");
		case EToolAccess::Control: return TEXT("Control");
		default: return TEXT("Unknown");
		}
	}

	FString ToSnakeCase(const FString& Identifier)
	{
		FString Result;
		const int32 Length = Identifier.Len();
		for (int32 Index = 0; Index < Length; ++Index)
		{
			const TCHAR Character = Identifier[Index];
			if (Character == TEXT('_'))
			{
				if (!Result.IsEmpty() && !Result.EndsWith(TEXT("_")))
				{
					Result.AppendChar(TEXT('_'));
				}
				continue;
			}

			if (FChar::IsUpper(Character) && Index > 0)
			{
				const TCHAR Previous = Identifier[Index - 1];
				const bool bAfterLowerOrDigit = FChar::IsLower(Previous) || FChar::IsDigit(Previous);
				const bool bAcronymEnd = FChar::IsUpper(Previous) && Index + 1 < Length && FChar::IsLower(Identifier[Index + 1]);
				if ((bAfterLowerOrDigit || bAcronymEnd) && !Result.IsEmpty() && !Result.EndsWith(TEXT("_")))
				{
					Result.AppendChar(TEXT('_'));
				}
			}
			Result.AppendChar(FChar::ToLower(Character));
		}
		return Result;
	}

	FReflectedTool::FReflectedTool()
		: InputSchema(MakeShared<FJsonObject>())
	{
	}

	TSharedPtr<FReflectedTool> FReflectedTool::Create(UClass* InToolsetClass, UFunction* InFunction, const FString& InToolsetName, FString& OutError, FString& OutWarning)
	{
		using namespace ReflectedToolPrivate;

		if (!InToolsetClass || !InFunction)
		{
			OutError = TEXT("missing class or function");
			return nullptr;
		}
		if (!InFunction->HasAnyFunctionFlags(FUNC_Static))
		{
			OutError = TEXT("is not static");
			return nullptr;
		}

		for (TFieldIterator<FProperty> It(InFunction); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			FString Reason;
			if (!Schema::IsSupportedProperty(*It, Reason))
			{
				OutError = Reason;
				return nullptr;
			}
		}

		TSharedRef<FReflectedTool> Tool = MakeShared<FReflectedTool>();

		const FString AccessText = InFunction->GetMetaData(TEXT("McpAccess"));
		if (AccessText.IsEmpty())
		{
			Tool->Access = EToolAccess::Write;
			OutWarning = TEXT("has no McpAccess metadata; treated as Write");
		}
		else if (!ParseAccess(AccessText, Tool->Access))
		{
			OutError = FString::Printf(TEXT("has invalid McpAccess '%s' (expected Read, Write, Destructive or Control)"), *AccessText);
			return nullptr;
		}

		Tool->bIsAsync = IsAsyncToolFunction(InFunction);
		if (Tool->bIsAsync && (Tool->Access == EToolAccess::Write || Tool->Access == EToolAccess::Destructive))
		{
			OutError = TEXT("returns UAgentMcpAsyncResult, which requires McpAccess Read or Control because an undo transaction cannot stay open across frames");
			return nullptr;
		}

		const bool bHasInputParameters = InFunction->NumParms > (InFunction->GetReturnProperty() ? 1 : 0);
		if (bHasInputParameters && !InFunction->HasAnyFunctionFlags(FUNC_BlueprintCallable))
		{
			// UE 5.5 UHT stores CPP_Default_ metadata only for BlueprintCallable or Exec functions (UhtFunctionParser.cs).
			OutWarning += OutWarning.IsEmpty() ? TEXT("") : TEXT("; ");
			OutWarning += TEXT("is not BlueprintCallable, so UE 5.5 records no C++ default values and every parameter is required");
		}

		const FString FunctionToolName = InFunction->HasMetaData(TEXT("McpName"))
			? InFunction->GetMetaData(TEXT("McpName"))
			: ToSnakeCase(InFunction->GetName());

		Tool->ToolsetName = InToolsetName;
		Tool->Name = InToolsetName + TEXT("_") + FunctionToolName;
		if (!IsValidAgentToolName(Tool->Name))
		{
			OutError = FString::Printf(TEXT("produces invalid tool name '%s' (use [a-z0-9_], at most %d characters)"), *Tool->Name, MaxToolNameLength);
			return nullptr;
		}

		Tool->bAllowInPIE = InFunction->HasMetaData(TEXT("McpAllowInPIE"));
		Tool->ToolsetClass = InToolsetClass;
		Tool->Function = InFunction;
		Tool->InputSchema = Schema::MakeInputSchema(InFunction);

		const FString Summary = Schema::MakeToolDescription(InFunction);
		Tool->Description = FString::Printf(TEXT("%s\n%s%s"),
			Summary.IsEmpty() ? TEXT("(no description)") : *Summary,
			AccessNote(Tool->Access),
			Tool->bIsAsync ? TEXT(" [Waits until the operation has finished]") : TEXT(""));
		return Tool;
	}

	TSharedPtr<FJsonObject> FReflectedTool::GetAnnotations() const
	{
		TSharedRef<FJsonObject> Annotations = MakeShared<FJsonObject>();
		Annotations->SetBoolField(TEXT("readOnlyHint"), Access == EToolAccess::Read);
		Annotations->SetBoolField(TEXT("destructiveHint"), Access == EToolAccess::Destructive);
		Annotations->SetBoolField(TEXT("openWorldHint"), false);
		return Annotations;
	}

	void FReflectedTool::Run(const TSharedRef<FJsonObject>& Arguments, const FAgentMcpCallContext& Context, FAgentMcpToolCompletion&& OnComplete)
	{
		if (Context.ClientTag.IsEmpty() || Access == EToolAccess::Read)
		{
			RunApproved(Arguments, Context, MoveTemp(OnComplete));
			return;
		}

		// A tagged client (the editor chat) asks before anything changes.
		const FAgentMcpApprovalGate* Gate = FindApprovalGate(Context.ClientTag);
		if (!Gate)
		{
			OnComplete(FAgentMcpToolResult::MakeError(TEXT("APPROVAL_UNAVAILABLE"),
				FString::Printf(TEXT("%s needs approval, and nothing is there to approve calls from client '%s'."), *Name, *Context.ClientTag),
				TEXT("Open the editor's AI chat panel, or connect without the X-AgentMcp-Client header.")));
			return;
		}

		FAgentMcpApprovalRequest Request;
		Request.ClientTag = Context.ClientTag;
		Request.ToolName = Name;
		Request.Access = LexToString(Access);
		Request.Arguments = Arguments;

		const TSharedRef<FReflectedTool> Self = StaticCastSharedRef<FReflectedTool>(AsShared());
		const TSharedRef<FAgentMcpToolCompletion> Completion = MakeShared<FAgentMcpToolCompletion>(MoveTemp(OnComplete));
		const FAgentMcpCallContext ContextCopy = Context;
		(*Gate)(Request, [Self, Arguments, ContextCopy, Completion](bool bApproved, const FString& Reason)
		{
			if (!*Completion)
			{
				return;
			}
			if (ContextCopy.IsCancelled())
			{
				(*Completion)(FAgentMcpToolResult::MakeError(TEXT("CANCELLED"), TEXT("The request was cancelled while waiting for approval.")));
			}
			else if (!bApproved)
			{
				(*Completion)(FAgentMcpToolResult::MakeError(TEXT("DENIED"),
					Reason.IsEmpty() ? FString::Printf(TEXT("The user did not approve %s."), *Self->Name) : Reason,
					TEXT("Do not retry the same call. Tell the user what you wanted to change, or ask what to do instead.")));
			}
			else
			{
				Self->RunApproved(Arguments, ContextCopy, MoveTemp(*Completion));
			}
			*Completion = nullptr;
		});
	}

	void FReflectedTool::RunApproved(const TSharedRef<FJsonObject>& Arguments, const FAgentMcpCallContext& Context, FAgentMcpToolCompletion&& OnComplete)
	{
		using namespace ReflectedToolPrivate;

		if (!IsEditorBusy())
		{
			Execute(Arguments, Context.CancelFlag, MoveTemp(OnComplete));
			return;
		}

		// Wait for package saving, garbage collection or async loading to finish (legacy bridge behaviour, KEEP-1).
		const double DeadlineSeconds = FPlatformTime::Seconds() + GetDefault<UAgentMcpSettings>()->BusyWaitTimeoutSeconds;
		const TSharedRef<FReflectedTool> Self = StaticCastSharedRef<FReflectedTool>(AsShared());
		const TSharedRef<FAgentMcpToolCompletion> Completion = MakeShared<FAgentMcpToolCompletion>(MoveTemp(OnComplete));
		const TSharedRef<bool> CancelFlag = Context.CancelFlag;

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[Self, Arguments, Completion, CancelFlag, DeadlineSeconds](float) -> bool
			{
				if (*CancelFlag)
				{
					(*Completion)(FAgentMcpToolResult::MakeError(TEXT("CANCELLED"), TEXT("The request was cancelled while waiting for the editor.")));
					return false;
				}
				if (!IsEditorBusy())
				{
					Self->Execute(Arguments, CancelFlag, MoveTemp(*Completion));
					return false;
				}
				if (FPlatformTime::Seconds() > DeadlineSeconds)
				{
					(*Completion)(FAgentMcpToolResult::MakeError(TEXT("EDITOR_BUSY"),
						TEXT("The editor stayed busy (saving, garbage collection or async loading)."),
						TEXT("Retry shortly, or raise BusyWaitTimeoutSeconds in the Agent MCP settings.")));
					return false;
				}
				return true;
			}), 0.1f);
	}

	void FReflectedTool::Execute(const TSharedRef<FJsonObject>& Arguments, const TSharedRef<bool>& CancelFlag, FAgentMcpToolCompletion&& OnComplete)
	{
		using namespace ReflectedToolPrivate;

		UClass* LiveClass = ToolsetClass.Get();
		UFunction* LiveFunction = Function.Get();
		if (!LiveClass || !LiveFunction)
		{
			OnComplete(FAgentMcpToolResult::MakeError(TEXT("TOOL_UNAVAILABLE"), FString::Printf(TEXT("%s is no longer available (module reloaded?)."), *Name)));
			return;
		}

		const UAgentMcpSettings* Settings = GetDefault<UAgentMcpSettings>();
		const bool bMutating = Access == EToolAccess::Write || Access == EToolAccess::Destructive;
		const bool bPlaySessionActive = GEditor && (GEditor->PlayWorld != nullptr || GEditor->IsPlaySessionInProgress());

		if (bMutating && bPlaySessionActive && !bAllowInPIE && !Settings->bAllowWritesDuringPIE)
		{
			OnComplete(FAgentMcpToolResult::MakeError(TEXT("PIE_ACTIVE"),
				FString::Printf(TEXT("%s changes editor data and is blocked while a play session is running."), *Name),
				TEXT("Stop the play session first (pie_stop).")));
			return;
		}

		FInvokeOutcome Outcome;
		bool bUndoRecorded = false;
		const FString TransactionTitle = FString::Printf(TEXT("AgentMcp: %s"), *Name);

		if (bMutating && GEditor && GEditor->Trans)
		{
			const int32 UndoableBefore = GetUndoableTransactionCount();
			{
				FScopedTransaction Transaction(FText::FromString(TransactionTitle));
				Outcome = InvokeToolFunction(LiveClass, LiveFunction, Arguments);
			}
			const bool bRecorded = GetUndoableTransactionCount() > UndoableBefore;
			if (!Outcome.bSuccess && bRecorded)
			{
				// Roll back partial writes so that a failed call leaves the editor unchanged. Cancelling the transaction would not
				// revert changes that were already applied.
				GEditor->UndoTransaction(/*bCanRedo=*/false);
				UE_LOG(LogAgentMcpToolset, Log, TEXT("%s failed; rolled back its partial changes."), *Name);
			}
			bUndoRecorded = Outcome.bSuccess && bRecorded;
		}
		else
		{
			Outcome = InvokeToolFunction(LiveClass, LiveFunction, Arguments);
		}

		if (!Outcome.bSuccess)
		{
			OnComplete(FAgentMcpToolResult::MakeError(Outcome.ErrorCode, Outcome.ErrorText, Outcome.ErrorHint));
			return;
		}

		if (Outcome.AsyncResult.IsValid())
		{
			WaitForAsyncResult(MoveTemp(Outcome.AsyncResult), CancelFlag, MoveTemp(OnComplete));
			return;
		}

		TSharedRef<FJsonObject> ResultObject = Outcome.Result.IsValid() ? Outcome.Result.ToSharedRef() : MakeShared<FJsonObject>();
		CompactToolResult(LiveFunction, *ResultObject);
		if (bMutating)
		{
			TSharedRef<FJsonObject> Undo = MakeShared<FJsonObject>();
			Undo->SetBoolField(TEXT("recorded"), bUndoRecorded);
			if (bUndoRecorded)
			{
				Undo->SetStringField(TEXT("transaction"), TransactionTitle);
			}
			ResultObject->SetObjectField(TEXT("undo"), Undo);
		}
		FAgentMcpToolResult ToolResult = MakeTextResult(ResultObject);
		for (const FAgentMcpImage& Image : Outcome.Images)
		{
			ToolResult.AddImage(Image.MimeType, Image.Data);
		}
		OnComplete(MoveTemp(ToolResult));
	}

	void FReflectedTool::WaitForAsyncResult(TStrongObjectPtr<UAgentMcpAsyncResult>&& Pending, const TSharedRef<bool>& CancelFlag, FAgentMcpToolCompletion&& OnComplete) const
	{
		using namespace ReflectedToolPrivate;

		UAgentMcpAsyncResult* Result = Pending.Get();
		if (Result->Tick())
		{
			OnComplete(MakeAsyncToolResult(*Result));
			return;
		}

		// The shared holder keeps the result alive across garbage collection until the ticker completes.
		const TSharedRef<TStrongObjectPtr<UAgentMcpAsyncResult>> Holder = MakeShared<TStrongObjectPtr<UAgentMcpAsyncResult>>(Result);
		const TSharedRef<FAgentMcpToolCompletion> Completion = MakeShared<FAgentMcpToolCompletion>(MoveTemp(OnComplete));
		const double DeadlineSeconds = FPlatformTime::Seconds() + Result->GetTimeoutSeconds() + AsyncTimeoutGraceSeconds;
		const FString ToolName = Name;

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[Holder, Completion, CancelFlag, DeadlineSeconds, ToolName](float) -> bool
			{
				UAgentMcpAsyncResult* PendingResult = Holder->Get();
				if (*CancelFlag)
				{
					(*Completion)(FAgentMcpToolResult::MakeError(TEXT("CANCELLED"), FString::Printf(TEXT("%s was cancelled before it finished."), *ToolName)));
					return false;
				}
				if (PendingResult->Tick())
				{
					(*Completion)(MakeAsyncToolResult(*PendingResult));
					return false;
				}
				if (FPlatformTime::Seconds() > DeadlineSeconds)
				{
					(*Completion)(FAgentMcpToolResult::MakeError(TEXT("TIMEOUT"), FString::Printf(TEXT("%s did not finish in time."), *ToolName)));
					return false;
				}
				return true;
			}));
	}
}
