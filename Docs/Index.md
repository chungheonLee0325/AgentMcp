# Agent MCP documentation

[한국어](Index.ko.md) · [README](../README.md)

Agent MCP runs an MCP server inside the Unreal Editor so agents can inspect and edit a project, run it, and review the outcome. Start here based on **what you want to do**.

## Getting started

| Goal | Read |
|---|---|
| Install the plugin and connect for the first time | [README Quick start](../README.md#quick-start) |
| Configure Claude Code, Codex, authentication, or multiple editors | [Connecting clients](Clients.md) |
| Follow task-oriented tool call sequences | [Workflows](Workflows.md) |
| Look up tool names and access levels | [Tool reference](Tools.md) |
| Set ports, tool access, and other project settings | [Settings](Settings.md) |
| Understand editing restrictions and approvals | [Safety](Safety.md) |

## By feature

| Area | Read | Covers |
|---|---|---|
| Widget Blueprints and UI | [Dungeon UI example](Samples/DungeonUi.md) | `BindWidget`, reusable components, data-driven lists, authoring and captures |
| Visual consistency | [UI style system](UiStyleSystem.md) | Theme Data Assets, styled widgets, review |
| Blueprints, levels, and assets | [Tools](Tools.md) | Graph inspection/editing, usage search, data and actors |
| Task guidance | [Skills](Skills.md) | Plugin and project `SKILL.md` files, overrides |
| In-editor AI chat | [AI chat](AiChat.md) | Providers, sign-in, permission modes, approvals |
| Extending the toolset | [Writing tools](WritingTools.md) | Adding Reflection-based MCP tools with `UFUNCTION` |
| Testing | [Testbed and smoke tests](Testing.md) | Running the included test project |

## How the pieces fit

```text
Claude Code / Codex             In-editor AI chat
         \                         /
          \                       /
           MCP calls (127.0.0.1)
                    |
             AgentMcpProtocol
                    |
          Reflection-based tools
                    |
          AgentMcpTools / Toolset
                    |
           Unreal Editor APIs
                    |
     Assets / UMG / Blueprint / PIE
```

Tool argument and result schemas are generated from C++ `UFUNCTION` declarations and Unreal Reflection. Task guidance is served separately as Markdown `SKILL.md` files. This separates **what a tool can do** from **how an agent should approach a production task**.

## Before making changes

- Tool access levels distinguish reads, edits, destructive operations, and editor controls. See [Tools](Tools.md) and [Safety](Safety.md).
- **Approval cards in the in-editor AI chat are not the same approval flow as a direct Claude Code or Codex connection.** The approval gate depends on the client tag.
- Asset transactions and **source-file edits have different recovery paths**. `source_replace` and `source_write` cannot be reverted with `editor_undo`; use source control.
- Confirm the target project via `editor_get_state` before changing it.

Documentation lives in the repository's `Docs/` folder. Links here point to those files directly; they do not imply a separate GitHub Wiki.
