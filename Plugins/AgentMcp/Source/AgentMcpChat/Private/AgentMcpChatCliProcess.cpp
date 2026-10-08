#include "AgentMcpChatCliProcess.h"

#include "Async/Async.h"
#include "HAL/RunnableThread.h"

namespace UE::AgentMcp::Chat
{
	namespace CliProcessPrivate
	{
		FString DecodeUtf8(const uint8* Bytes, int32 Count)
		{
			const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes), Count);
			return FString::ConstructFromPtrSize(Converted.Get(), Converted.Length());
		}
	}

	TSharedPtr<FCliProcess, ESPMode::ThreadSafe> FCliProcess::Launch(const FString& Executable, const FString& Arguments,
		const FString& WorkingDirectory, FOnLine OnLine, FOnExit OnExit, FString& OutError, bool bWithInput)
	{
		TSharedPtr<FCliProcess, ESPMode::ThreadSafe> Result(new FCliProcess());
		Result->WeakSelf = Result;
		Result->OnLine = MoveTemp(OnLine);
		Result->OnExit = MoveTemp(OnExit);

		if (!FPlatformProcess::CreatePipe(Result->ReadPipe, Result->WritePipe))
		{
			OutError = TEXT("프로세스 출력 파이프를 만들지 못했습니다.");
			return nullptr;
		}

		if (bWithInput && !FPlatformProcess::CreatePipe(Result->InputReadPipe, Result->InputWritePipe, /*bWritePipeLocal=*/true))
		{
			OutError = TEXT("프로세스 입력 파이프를 만들지 못했습니다.");
			return nullptr;
		}

		// stdout and stderr both go to the pipe.
		Result->Process = FPlatformProcess::CreateProc(*Executable, *Arguments, /*bLaunchDetached=*/false, /*bLaunchHidden=*/true,
			/*bLaunchReallyHidden=*/true, nullptr, 0, *WorkingDirectory, Result->WritePipe, Result->InputReadPipe);
		if (!Result->Process.IsValid())
		{
			OutError = FString::Printf(TEXT("%s을(를) 실행하지 못했습니다."), *Executable);
			return nullptr;
		}

		Result->Thread = FRunnableThread::Create(Result.Get(), TEXT("AgentMcpChatCliProcess"));
		return Result;
	}

	FCliProcess::~FCliProcess()
	{
		bStopping = true;
		if (Process.IsValid() && FPlatformProcess::IsProcRunning(Process))
		{
			FPlatformProcess::TerminateProc(Process, /*KillTree=*/true);
		}
		if (Thread)
		{
			Thread->WaitForCompletion();
			delete Thread;
		}
		if (Process.IsValid())
		{
			FPlatformProcess::CloseProc(Process);
		}
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		if (InputReadPipe || InputWritePipe)
		{
			FPlatformProcess::ClosePipe(InputReadPipe, InputWritePipe);
		}
	}

	bool FCliProcess::WriteLine(const FString& Line)
	{
		return InputWritePipe && !Line.IsEmpty() && FPlatformProcess::WritePipe(InputWritePipe, Line);
	}

	uint32 FCliProcess::Run()
	{
		TArray<uint8> Chunk;
		while (!bStopping && FPlatformProcess::IsProcRunning(Process))
		{
			if (FPlatformProcess::ReadPipeToArray(ReadPipe, Chunk))
			{
				Pending.Append(Chunk);
				FlushLines(/*bFinal=*/false);
			}
			else
			{
				FPlatformProcess::Sleep(0.02f);
			}
		}

		// Output written just before the exit is still in the pipe.
		while (FPlatformProcess::ReadPipeToArray(ReadPipe, Chunk))
		{
			Pending.Append(Chunk);
		}
		FlushLines(/*bFinal=*/true);

		int32 ReturnCode = -1;
		if (!bStopping)
		{
			FPlatformProcess::GetProcReturnCode(Process, &ReturnCode);
		}

		const TWeakPtr<FCliProcess, ESPMode::ThreadSafe> Weak = WeakSelf;
		const bool bStopped = bStopping;
		AsyncTask(ENamedThreads::GameThread, [Weak, ReturnCode, bStopped]()
		{
			const TSharedPtr<FCliProcess, ESPMode::ThreadSafe> This = Weak.Pin();
			if (This && !bStopped && This->OnExit)
			{
				This->OnExit(ReturnCode);
			}
		});
		return 0;
	}

	void FCliProcess::FlushLines(bool bFinal)
	{
		using CliProcessPrivate::DecodeUtf8;

		int32 LineStart = 0;
		for (int32 Index = 0; Index < Pending.Num(); ++Index)
		{
			if (Pending[Index] == '\n')
			{
				int32 LineEnd = Index;
				if (LineEnd > LineStart && Pending[LineEnd - 1] == '\r')
				{
					--LineEnd;
				}
				PostLine(DecodeUtf8(Pending.GetData() + LineStart, LineEnd - LineStart));
				LineStart = Index + 1;
			}
		}
		if (bFinal && LineStart < Pending.Num())
		{
			PostLine(DecodeUtf8(Pending.GetData() + LineStart, Pending.Num() - LineStart));
			LineStart = Pending.Num();
		}
		Pending.RemoveAt(0, LineStart);
	}

	void FCliProcess::PostLine(FString Line)
	{
		if (Line.IsEmpty() || bStopping)
		{
			return;
		}
		const TWeakPtr<FCliProcess, ESPMode::ThreadSafe> Weak = WeakSelf;
		AsyncTask(ENamedThreads::GameThread, [Weak, Line = MoveTemp(Line)]()
		{
			const TSharedPtr<FCliProcess, ESPMode::ThreadSafe> This = Weak.Pin();
			if (This && !This->bStopping && This->OnLine)
			{
				This->OnLine(Line);
			}
		});
	}

	FString QuoteCommandLineArgument(const FString& Argument)
	{
		FString Result = TEXT("\"");
		int32 Backslashes = 0;
		for (const TCHAR Character : Argument)
		{
			if (Character == TEXT('\\'))
			{
				++Backslashes;
				continue;
			}
			if (Character == TEXT('"'))
			{
				// Backslashes before a quote are doubled, and the quote itself is escaped.
				Result += FString::ChrN(Backslashes * 2 + 1, TEXT('\\'));
			}
			else
			{
				Result += FString::ChrN(Backslashes, TEXT('\\'));
			}
			Result.AppendChar(Character);
			Backslashes = 0;
		}
		// Backslashes before the closing quote are doubled so they do not escape it.
		Result += FString::ChrN(Backslashes * 2, TEXT('\\'));
		Result.AppendChar(TEXT('"'));
		return Result;
	}
}
