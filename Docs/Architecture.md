# MCP architecture — connecting AI agents to Unreal Editor

[한국어](Architecture.ko.md) · [Documentation index](Index.md) · [Writing tools](WritingTools.md)

AgentMcp **runs an MCP server inside the Unreal Editor process** and exposes C++ `UFUNCTION` methods as MCP tools. External agents such as Claude Code and Codex, as well as the in-editor AI chat, call the same tools. The editor-side server handles tool discovery, invocation and access checks for both external agents and the in-editor chat.

It takes inspiration from Unreal Engine 5.8's experimental MCP Toolset and Agent Skill concepts, but is **independently implemented for UE 5.5/5.7 environments**. It does not copy Epic's plugin source or expose an identical UE 5.8 API.

## Architecture overview

```text
External AI agents                  In-editor AI chat panel
Claude Code / Codex                Claude Code / Codex / Gemini etc.
        |                                         |
        | MCP client                       AgentMcpChat
        |                                  (local MCP client)
        +--------------------+--------------------+
                             |
                 127.0.0.1:<port>/mcp
                  Streamable HTTP + JSON
                             |
                      AgentMcpProtocol
               Sessions / tools/list / tools/call
                             |
                       AgentMcpToolset
             Reflection-based tool discovery
               Access / approval / transactions
                             |
                        AgentMcpTools
             Actors / Assets / Blueprints / UMG
               PIE / Logs / Viewport / Perf
                             |
                    Unreal Editor APIs
                    Project assets and state

        Agent Skills (SKILL.md)
        └─ Served separately via skills_list / skills_get
           → Task guidance, not direct asset modification
```

The in-editor chat manages providers and the conversation UI; it does not implement a separate Unreal editing API. Its `FMcpClient` connects to **the MCP endpoint of the same running editor** via `initialize` → `tools/list` → `tools/call`.

## Module responsibilities

| Module | Responsibility | Source |
|---|---|---|
| `AgentMcpProtocol` | HTTP transport, MCP/JSON-RPC messages, sessions, requests and responses | [`AgentMcpServer.cpp`](../Plugins/AgentMcp/Source/AgentMcpProtocol/Private/AgentMcpServer.cpp) |
| `AgentMcpToolset` | Automatic tool discovery, Reflection schemas and invocation, execution policies | [`AgentMcpRuntime.cpp`](../Plugins/AgentMcp/Source/AgentMcpToolset/Private/AgentMcpRuntime.cpp), [`AgentMcpReflectedTool.cpp`](../Plugins/AgentMcp/Source/AgentMcpToolset/Private/AgentMcpReflectedTool.cpp) |
| `AgentMcpTools` | Concrete functionality using Unreal Editor APIs | [`Toolsets/`](../Plugins/AgentMcp/Source/AgentMcpTools/Private/Toolsets) |
| `AgentMcpChat` | Editor panel, model providers, MCP client and approval UI | [`AgentMcpChatMcpClient.cpp`](../Plugins/AgentMcp/Source/AgentMcpChat/Private/AgentMcpChatMcpClient.cpp) |
| `AgentMcpCompat` | Version compatibility helpers and JSON/UTF-8 conversions | [`AgentMcpCompat.h`](../Plugins/AgentMcp/Source/AgentMcpCompat/Public/AgentMcpCompat.h) |

### 1. Startup and tool registration

After editor initialization, `AgentMcpToolsetModule` calls `FAgentMcpRuntime::Startup`. The runtime builds its tool catalog as follows:

1. `GetDerivedClasses(UAgentMcpToolset::StaticClass(), …)` finds toolset subclasses.
2. Each class contributes `static UFUNCTION` methods marked `AICallable`; functions marked `AIIgnore` are skipped.
3. `FReflectedTool::Create` validates parameter and result types, then builds JSON input schemas, tool names, descriptions and access levels.
4. `AllowedTools` and `BlockedTools` filter the set exposed by the server.

New functions can be added to a `UAgentMcpToolset` subclass **without updating a central server registration list**. Toolset and function names produce MCP names such as `umg_inspect` and `blueprint_compile`.

```cpp
UCLASS(meta = (McpToolset = "my"))
class UMyTools : public UAgentMcpToolset
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,
        meta = (AICallable, McpAccess = "Read"))
    static FMyResult Inspect(const FString& AssetPath);
};
```

This is a declaration example; `FMyResult` and the implementation are omitted. See [Writing tools](WritingTools.md) for a complete template and requirements.

With `ExposureMode=Native`, each tool is exposed individually. `ToolSearch` exposes only `toolsets_list`, `toolsets_describe` and `tools_call` and lets the agent request detailed schemas when needed. Both modes use the same underlying catalog.

### 2. Calling a tool and invoking Unreal functions

The execution path of a tool call:

```text
MCP tools/call(name, arguments)
  → FAgentMcpServerImpl::HandleToolsCall
  → Find tool / associate session and cancellation state
  → FReflectedTool::Run
  → Check access and applicable approval gate
  → Check editor state (PIE, saving, GC, async loading)
  → InvokeToolFunction
  → Convert Reflection arguments / invoke UFunction / serialize result
  → Return tool result or structured error
```

`FReflectedTool` owns **tool metadata and execution policy**. [`AgentMcpInvoker.cpp`](../Plugins/AgentMcp/Source/AgentMcpToolset/Private/AgentMcpInvoker.cpp) converts JSON values to Unreal types and invokes the function. [`AgentMcpSchema.cpp`](../Plugins/AgentMcp/Source/AgentMcpToolset/Private/AgentMcpSchema.cpp) generates the input schema from `UFunction` and `UPROPERTY` metadata.

Editor object operations run on the **game thread**. If the editor is busy saving, collecting garbage or loading assets asynchronously, calls wait up to `BusyWaitTimeoutSeconds` or return `EDITOR_BUSY`. Operations such as PIE startup or compilation that finish on a later frame may use `UAgentMcpAsyncResult` with the Ticker for completion, cancellation and timeout. This asynchronous result mechanism is limited to **Read/Control tools** because an editor undo transaction cannot span frames.

### 3. MCP transport and sessions

The server uses Unreal's `HTTPServer`, by default at `http://127.0.0.1:18765/mcp`. It handles MCP JSON-RPC via `POST` and closes sessions via `DELETE`. `initialize` returns protocol details, server capabilities and agent instructions. `tools/list` returns available tool schemas; `tools/call` executes a selected operation.

Responses are **plain JSON**. The HTTPServer path used for UE 5.5 does not offer an SSE response stream, so `GET` streaming and progress notifications are not supported. The server handles request cancellation and session closure, but it does not initiate arbitrary new tasks on external clients.

## Editing and approval boundaries

`FReflectedTool` distinguishes `Read`, `Write`, `Destructive` and `Control` tools via `McpAccess`.

- **Read:** Inspect project state and assets; no change approval.
- **Write / Destructive:** Edits are blocked during PIE by default. Supported Unreal changes are recorded with `FScopedTransaction`, and recorded partial transactions are rolled back on failure.
- **Control:** Actions such as opening assets, editor selection or PIE follow a different policy from data edits.
- **Saving and recovery:** Ordinary asset edits are not saved automatically; `asset_save` and `level_save` are separate. File-system edits via `source_write` and `source_replace` cannot be reverted through `editor_undo`.

**Approvals differ between in-editor chat and direct MCP clients.** The chat's `FMcpClient` sends an `X-AgentMcp-Client` tag. Non-Read calls from tagged sessions go through the registered approval gate, which displays approval/denial cards in the chat panel. **Direct Claude Code or Codex connections without that tag do not automatically use those cards.** Use `AllowedTools`, `BlockedTools` and `BlockedProperties` for project-wide restrictions.

[Safety and permissions](Safety.md) · [AI chat](AiChat.md)

## Agent Skills are a separate guidance layer

Tool registration and Skill loading are independent. `AgentMcpRuntime` embeds the available Skill names and descriptions in initial server instructions; an agent requests the relevant instructions with `skills_list` and `skills_get`.

`skills_get` reads the actual `SKILL.md` at call time, and a project-level Skill of the same name can replace the plugin's default guidance. **Skills tell an agent how to use tools; they do not execute Unreal edits by themselves.**

[Dynamic Skill loading and overrides](Skills.md)

## Design decisions and scope

- **Unreal Reflection:** Generating schemas from C++ declarations reduces duplicated API definitions and manual registration, at the cost of type and metadata constraints.
- **Local server inside the editor:** Allows direct access to assets, Blueprint, UMG and PIE, while inheriting the editor's runtime state and game-thread constraints.
- **One tool layer, multiple clients:** Direct MCP connections and in-editor chat share capabilities, but not necessarily the same connection or approval policy.
- **Tools separate from Skills:** Tools perform operations; Markdown Skills carry project-specific guidance and workflow decisions.

[Connecting clients](Clients.md) · [Tool reference](Tools.md) · [Dungeon UI example](Samples/DungeonUi.md)
