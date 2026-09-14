#pragma once

#include "CoreMinimal.h"
#include "IAgentMcpTool.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UAgentMcpAsyncResult;
class UClass;
class UFunction;

namespace UE::AgentMcp
{
	/** McpAccess metadata of a tool function; drives the dispatcher policy (TARGET_ARCHITECTURE.md §5.4). */
	enum class EToolAccess : uint8
	{
		Read,
		Write,
		Destructive,
		Control,
	};

	const TCHAR* LexToString(EToolAccess Access);

	/** "GetState" -> "get_state", "GetUIState" -> "get_ui_state". */
	FString ToSnakeCase(const FString& Identifier);

	/**
	 * MCP tool backed by a static UFUNCTION(meta=(AICallable)) on a UAgentMcpToolset subclass.
	 * Run applies the dispatcher policies: busy wait, PIE guard, undoable transaction with rollback on failure,
	 * waiting for asynchronous results, result size cap.
	 */
	class FReflectedTool final : public IAgentMcpTool
	{
	public:
		/** Returns null with OutError when the function cannot be exposed. OutWarning reports recoverable authoring issues. */
		static TSharedPtr<FReflectedTool> Create(UClass* InToolsetClass, UFunction* InFunction, const FString& InToolsetName, FString& OutError, FString& OutWarning);

		//~ Begin IAgentMcpTool
		virtual FString GetName() const override { return Name; }
		virtual FString GetDescription() const override { return Description; }
		virtual TSharedRef<FJsonObject> GetInputSchema() const override { return InputSchema; }
		virtual TSharedPtr<FJsonObject> GetAnnotations() const override;
		virtual void Run(const TSharedRef<FJsonObject>& Arguments, const FAgentMcpCallContext& Context, FAgentMcpToolCompletion&& OnComplete) override;
		//~ End IAgentMcpTool

		const FString& GetToolsetName() const { return ToolsetName; }
		EToolAccess GetAccess() const { return Access; }

		/** Executes now, without waiting for the editor to become idle. Asynchronous tools complete on a later frame. */
		void Execute(const TSharedRef<FJsonObject>& Arguments, const TSharedRef<bool>& CancelFlag, FAgentMcpToolCompletion&& OnComplete);

		FReflectedTool();

	private:
		/** Ticks the result every frame until it finishes, the client cancels, or its timeout plus a grace period expires. */
		void WaitForAsyncResult(TStrongObjectPtr<UAgentMcpAsyncResult>&& Pending, const TSharedRef<bool>& CancelFlag, FAgentMcpToolCompletion&& OnComplete) const;

		FString Name;
		FString ToolsetName;
		FString Description;
		TSharedRef<FJsonObject> InputSchema;
		EToolAccess Access = EToolAccess::Write;
		bool bAllowInPIE = false;
		bool bIsAsync = false;
		TWeakObjectPtr<UClass> ToolsetClass;
		TWeakObjectPtr<UFunction> Function;
	};
}
