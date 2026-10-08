#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"

class FRunnableThread;

namespace UE::AgentMcp::Chat
{
	/**
	 * A child process whose output is reported line by line on the game thread. FMonitoredProcess decodes each pipe read on its
	 * own, which breaks multibyte UTF-8 characters split across reads; this splits the raw bytes at newlines before decoding.
	 * Destroying it ends the process tree.
	 */
	class FCliProcess final : public FRunnable, public TSharedFromThis<FCliProcess, ESPMode::ThreadSafe>
	{
	public:
		using FOnLine = TFunction<void(const FString& Line)>;
		using FOnExit = TFunction<void(int32 ReturnCode)>;

		/**
		 * Returns null with OutError set when the process could not start. bWithInput gives the child a stdin pipe for WriteLine;
		 * without it stdin is left unset, so a CLI that reads stdin when it is a pipe never waits for it.
		 */
		static TSharedPtr<FCliProcess, ESPMode::ThreadSafe> Launch(const FString& Executable, const FString& Arguments, const FString& WorkingDirectory,
			FOnLine OnLine, FOnExit OnExit, FString& OutError, bool bWithInput = false);

		/** Writes a line to the child's stdin. Only with bWithInput. */
		bool WriteLine(const FString& Line);

		virtual ~FCliProcess() override;

		virtual uint32 Run() override;
		virtual void Stop() override { bStopping = true; }

	private:
		FCliProcess() = default;

		/** Posts every complete line in Pending to the game thread. */
		void FlushLines(bool bFinal);
		void PostLine(FString Line);

		/** Set on the game thread before the thread starts; the thread posts through it so a destroyed process gets no callbacks. */
		TWeakPtr<FCliProcess, ESPMode::ThreadSafe> WeakSelf;
		FProcHandle Process;
		void* ReadPipe = nullptr;
		void* WritePipe = nullptr;
		void* InputReadPipe = nullptr;
		void* InputWritePipe = nullptr;
		FRunnableThread* Thread = nullptr;
		TArray<uint8> Pending;
		FOnLine OnLine;
		FOnExit OnExit;
		std::atomic<bool> bStopping = false;
	};

	/** Quotes one argument for a Windows command line, following the CommandLineToArgvW rules. */
	FString QuoteCommandLineArgument(const FString& Argument);
}
