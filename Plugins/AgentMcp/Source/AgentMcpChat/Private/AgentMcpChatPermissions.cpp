#include "AgentMcpChatPermissions.h"

namespace UE::AgentMcp::Chat
{
	const TCHAR* const ChatClientTag = TEXT("agent-mcp-chat");

	namespace PermissionsPrivate
	{
		bool IsAssetTool(const FString& ToolName)
		{
			static const TCHAR* const Prefixes[] = { TEXT("actor_"), TEXT("anim_"), TEXT("asset_"), TEXT("blueprint_"), TEXT("datatable_"),
				TEXT("level_"), TEXT("object_"), TEXT("stringtable_"), TEXT("umg_") };
			for (const TCHAR* Prefix : Prefixes)
			{
				if (ToolName.StartsWith(Prefix))
				{
					return true;
				}
			}
			return ToolName == TEXT("editor_undo") || ToolName == TEXT("editor_redo");
		}

		bool IsCodeTool(const FString& ToolName)
		{
			return ToolName == TEXT("source_write") || ToolName == TEXT("source_replace") || ToolName == TEXT("livecoding_compile");
		}
	}

	bool IsNavigationTool(const FString& ToolName)
	{
		return ToolName == TEXT("editor_open_assets") || ToolName == TEXT("editor_show_in_content_browser") || ToolName == TEXT("actor_select")
			|| ToolName == TEXT("viewport_set_camera");
	}

	bool IsToolAllowedInMode(const FString& ToolName, bool bReadOnly, EAgentMcpChatMode Mode)
	{
		using namespace PermissionsPrivate;

		// tools_call can run any tool by name, so it is never offered; the meta tools of ToolSearch mode are not needed here.
		if (ToolName == TEXT("tools_call"))
		{
			return false;
		}
		// Looking around changes no project data, so every mode may open, show and frame things. Opening another level replaces the
		// open one, so it is allowed everywhere but asks first.
		if (bReadOnly || IsNavigationTool(ToolName) || ToolName == TEXT("level_open"))
		{
			return true;
		}
		const bool bAssets = Mode == EAgentMcpChatMode::AssetEdit || Mode == EAgentMcpChatMode::AssetAndCodeEdit;
		const bool bCode = Mode == EAgentMcpChatMode::CodeEdit || Mode == EAgentMcpChatMode::AssetAndCodeEdit;
		return (bAssets && IsAssetTool(ToolName)) || (bCode && IsCodeTool(ToolName));
	}

	FString GetModeLabel(EAgentMcpChatMode Mode)
	{
		switch (Mode)
		{
		case EAgentMcpChatMode::AssetEdit: return TEXT("에셋 편집");
		case EAgentMcpChatMode::CodeEdit: return TEXT("코드 편집");
		case EAgentMcpChatMode::AssetAndCodeEdit: return TEXT("에셋 + 코드 편집");
		default: return TEXT("읽기 전용");
		}
	}

	void FPendingApproval::Resolve(bool bApproved, const FString& Reason)
	{
		if (bResolved)
		{
			return;
		}
		bResolved = true;
		if (Decide)
		{
			Decide(bApproved, Reason);
			Decide = nullptr;
		}
	}

	FChatApprovals& FChatApprovals::Get()
	{
		static FChatApprovals Instance;
		return Instance;
	}

	void FChatApprovals::Register()
	{
		SetApprovalGate(ChatClientTag, [](const FAgentMcpApprovalRequest& Request, FAgentMcpApprovalDecision Decide)
		{
			FChatApprovals::Get().HandleRequest(Request, MoveTemp(Decide));
		});
	}

	void FChatApprovals::Unregister()
	{
		DenyAll(TEXT("The editor chat closed."));
		ClearApprovalGate(ChatClientTag);
	}

	void FChatApprovals::DenyAll(const FString& Reason)
	{
		const TArray<TSharedRef<FPendingApproval>> Waiting = MoveTemp(Pending);
		Pending.Reset();
		for (const TSharedRef<FPendingApproval>& Approval : Waiting)
		{
			Approval->Resolve(false, Reason);
		}
	}

	void FChatApprovals::HandleRequest(const FAgentMcpApprovalRequest& Request, FAgentMcpApprovalDecision Decide)
	{
		// The mode is enforced here as well as in what the client offers, so a client that offers more cannot use it.
		const EAgentMcpChatMode Mode = GetDefault<UAgentMcpChatSettings>()->Mode;
		if (!IsToolAllowedInMode(Request.ToolName, /*bReadOnly=*/false, Mode))
		{
			Decide(false, FString::Printf(TEXT("%s is not allowed in the chat's current permission mode (%s). Explain the change to the user "
				"instead; they can switch the mode in the chat panel."), *Request.ToolName, *GetModeLabel(Mode)));
			return;
		}
		if (bApproveAll || IsNavigationTool(Request.ToolName))
		{
			Decide(true, FString());
			return;
		}
		if (!OnRequest)
		{
			Decide(false, TEXT("No chat panel is open to approve this change."));
			return;
		}

		TSharedRef<FPendingApproval> Approval = MakeShared<FPendingApproval>();
		Approval->Request = Request;
		// Drop the request from the waiting list once it is decided, however that happens.
		Approval->Decide = [this, WeakApproval = TWeakPtr<FPendingApproval>(Approval), Inner = MoveTemp(Decide)](bool bApproved, const FString& Reason)
		{
			Pending.RemoveAll([&WeakApproval](const TSharedRef<FPendingApproval>& Item) { return WeakApproval.Pin() == Item; });
			Inner(bApproved, Reason);
		};
		Pending.Add(Approval);
		OnRequest(Approval);
	}
}
