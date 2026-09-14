#pragma once

#include "CoreMinimal.h"

namespace UE::AgentMcp
{
	/**
	 * Collects the first RaiseToolError raised on this thread while the scope is alive.
	 * Scopes nest; the innermost scope receives the error.
	 */
	class FToolErrorScope
	{
	public:
		FToolErrorScope();
		~FToolErrorScope();

		FToolErrorScope(const FToolErrorScope&) = delete;
		FToolErrorScope& operator=(const FToolErrorScope&) = delete;

		bool HasError() const { return bHasError; }
		const FString& GetErrorCode() const { return ErrorCode; }
		const FString& GetErrorText() const { return ErrorText; }
		const FString& GetErrorHint() const { return ErrorHint; }

		void SetError(const FString& InCode, const FString& InText, const FString& InHint);

		static FToolErrorScope* GetCurrent();

	private:
		FToolErrorScope* Previous = nullptr;
		bool bHasError = false;
		FString ErrorCode;
		FString ErrorText;
		FString ErrorHint;
	};
}
