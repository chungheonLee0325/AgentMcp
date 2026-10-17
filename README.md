# Agent MCP — MCP server for Unreal Editor

[한국어](README.ko.md) · [Documentation](Docs/Index.md) · [Workflows](Docs/Workflows.md)

Agent MCP is an Unreal Editor plugin that lets **AI coding agents such as Claude Code and Codex inspect and modify a real Unreal project**. It exposes assets, Blueprints, UMG, and levels through MCP tools and supports compilation, Play In Editor (PIE), logs, and viewport captures to review changes.

```text
Inspect project → Edit → Compile → Run PIE → Review logs/capture → Iterate
```

**Status: beta.** Built and tested on Windows 64-bit with Unreal Engine **5.5.4**. Also built and used in a project on **5.7.4**, but the full smoke test has not been run there. Other platforms have not been tested.

## What you can do

| Task | Agent MCP tools |
|---|---|
| **Inspect projects and assets** | Find actors, assets, and classes; inspect properties, dependencies and references; locate assets in the Content Browser |
| **Author and edit UMG** | Inspect Widget Trees and Named Slots, validate C++ `BindWidget` contracts, create Widget Blueprints and edit widget/slot properties |
| **Edit Blueprint logic** | Read graphs, find and connect node types, add members, edit graph operations, search usages with `blueprint_find_usages` |
| **Work with source** | `source_find` / `source_search` / `source_read`, constrained text edits, encoding preservation including CP949 |
| **Create content** | Levels and actors, DataTables and Data Assets, string tables, texture/mesh import and selected animation assets |
| **Run and review** | Blueprint compilation, Live Coding, PIE, editor logs and viewport captures including game UI |
| **Investigate performance** | CSV profiling and run comparison, Unreal Insights timers, GPU profile dumps, machine checks |

[Tool reference and access levels](Docs/Tools.md) · [Task-oriented workflows](Docs/Workflows.md)

## Quick start

### 1. Install in your project

1. Copy this repository's `Plugins/AgentMcp` directory to `Plugins/AgentMcp` in your Unreal project.
2. Enable **Agent MCP** in **Edit > Plugins**, or add `{"Name": "AgentMcp", "Enabled": true}` to the existing `Plugins` array in your `.uproject`.
3. The plugin ships as source. **Build your project's Editor target**, then launch the editor.

### 2. Check the server connection

Look for a log line of this form in the editor Output Log:

```text
LogAgentMcpProtocol: Agent MCP server listening on http://127.0.0.1:18765/mcp
```

The default port is `18765`; the testbed in this repository uses `18766`. The editor must remain open because the MCP server runs inside it.

On startup, the editor writes a `.mcp.json` for Claude Code and a `.codex/config.toml` for Codex into the project directory. Codex reads project configuration only in **trusted project folders**. For token authentication, multiple editors, or manual configuration, see [Connecting a client](Docs/Clients.md).

### 3. Make the first call

Ask the connected agent:

> Call `editor_get_state` and tell me the current Unreal project, open level, PIE state, and unsaved changes. Do not edit anything yet.

Next, use `asset_find` to locate assets, `blueprint_inspect` for Blueprint structure, or `umg_inspect` for a Widget Blueprint. Confirm the target project and change scope before editing.

## Example workflows

**Check the impact of a Blueprint function change**

```text
editor_get_state
  → blueprint_inspect
  → blueprint_find_usages
  → blueprint_get_graph / blueprint_read_graph
  → review callers before editing
```

**Create and review a UMG screen**

```text
umg_create_widget_blueprint
  → umg_add_widgets
  → blueprint_compile
  → pie_start → viewport_capture → log_get_recent
  → pie_stop → asset_save
```

See [Workflows](Docs/Workflows.md) for argument guidance and the precautions for each sequence.

## Dungeon UI — learning from an actual authoring workflow

`Content/Samples/DungeonUi` contains a dungeon HUD and result popup built by Claude Code using Agent MCP tools rather than manually editing the UMG Designer.

![Dungeon result popup](Docs/Images/dungeon_result.jpg)

![Dungeon progress HUD](Docs/Images/dungeon_hud.jpg)

The initial version duplicated repeated elements and scattered style values across the widget trees. It was rebuilt around **reusable Widget Blueprint components, C++ `BindWidget` contracts, `DynamicEntryBox`, a Theme Data Asset, and DataTables**. The work also exposed verbose tool read-back, which was reduced, while reusable authoring rules were moved into the `umg-authoring` Skill.

[Dungeon UI case study](Docs/Samples/DungeonUi.md) · [UI style system](Docs/UiStyleSystem.md)

## Performance investigation — from measurement to comparison

The `perf-investigation` Skill runs **repeatable fixed scenes in a standalone game**, while tools analyze the resulting files. `perf_summarize_csv` and `perf_compare_csv` summarize profiler frames and events and account for variation between runs. `perf_trace_hotspots` analyzes timers from Unreal Insights traces; `perf_read_gpu_profile` reads `ProfileGPU` dumps.

```text
Fixed scenes → Baseline runs → Mark moments with CSV_EVENT → Analyze trace
      → Fix cause → Repeat under the same conditions → Compare against run noise
```

Examples from Sonheim (1920×1080, event medians; frame time with game-thread time in parentheses):

| Moment | Before | After | Cause identified |
|---|---|---|---|
| Swap two inventory items | 32.6ms (19.4ms) | 16.5ms (2.9ms) | all slots rebuilt and tooltips reattached |
| Open inventory | 57.5ms (44.6ms) | 21.7ms (8.1ms) | screen, 42 slots, and per-item tooltips recreated |

The workflow is designed around repeatable conditions and variation across runs, rather than treating one run as evidence. [Performance workflow](Docs/Workflows.md#6-measure-and-compare-performance)

## Separate tools from task guidance

Agent MCP tools are `static UFUNCTION` methods on `UAgentMcpToolset` subclasses. Tool names, arguments, and result JSON schemas are generated using Unreal Reflection. See [Writing tools](Docs/WritingTools.md) to extend the toolset.

**Tools expose operations**; **Skills specify how to approach a task using those operations**. The plugin serves Markdown `SKILL.md` files, and project-specific files can override the defaults.

Included Skills:
- `umg-authoring`: reusable components, `BindWidget`, data-driven lists and review
- `ui-style-system`: design tokens, style extraction/application, and gallery
- `ui-art-requests`: request, review, import and connect visual assets
- `blueprint-graphs`: Blueprint class/graph authoring and runtime verification
- `perf-investigation`: repeatable scene runs, investigation and before/after comparison against run variation

[Skill locations and behavior](Docs/Skills.md)

## AI chat inside the editor

The **Window > Agent MCP > AI Chat** panel lets users ask questions and request changes without opening a separate Claude Code/Codex MCP client. It supports CLI sign-in or API-based providers, permission modes, and change approval cards.

**Important:** Approval cards in the chat panel are **not automatically enforced** for external clients connected directly over MCP. See [AI chat](Docs/AiChat.md) and [Safety](Docs/Safety.md) for how permissions differ.

## Documentation

Browse the [documentation index](Docs/Index.md) by goal or use these links:

- [Connecting clients](Docs/Clients.md) · [Settings](Docs/Settings.md) · [Safety](Docs/Safety.md)
- [All tools](Docs/Tools.md) · [Workflows](Docs/Workflows.md) · [Agent Skills](Docs/Skills.md)
- [AI chat](Docs/AiChat.md) · [Writing tools](Docs/WritingTools.md) · [Testbed and smoke tests](Docs/Testing.md)

## Current limitations

- Requests run on the editor game thread. **Use Less CPU when in Background** may slow down calls to an unfocused editor.
- Responses are plain JSON; SSE and progress streaming are not provided.
- `source_replace` and `source_write` cannot be reverted through Unreal Editor Undo. Use Git or other source control.
- Live Coding cannot apply reflected `UCLASS`, `USTRUCT`, `UPROPERTY` or `UFUNCTION` declaration changes.
- Some Blueprint member operations, automatic graph layout, and UMG animation editing remain outside the toolset.
- Claude Code was used for the main sample workflow. Codex has been used through the AI chat panel; a direct connection through `.codex/config.toml` has not been separately tested.

See [Tools](Docs/Tools.md), [Safety](Docs/Safety.md), and [Testing](Docs/Testing.md) for detailed scope.

## Background and license

The design takes inspiration from Epic Games' experimental UE 5.8 MCP Toolset and Agent Skill concepts but is **independently implemented for UE 5.5**. It is not a port of Epic's plugin source or `UAgentSkill`. Unreal Engine is a trademark of Epic Games, Inc.

[MIT License](LICENSE)
