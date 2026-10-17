# Agent MCP for Unreal Engine 5.5

[한국어](README.ko.md)

Agent MCP runs a [Model Context Protocol](https://modelcontextprotocol.io) server inside Unreal Engine 5.5, allowing coding agents such as Claude Code and Codex to **inspect a project, modify it, run it, and verify the result**.

Rather than being only a remote-control layer for Unreal, the plugin focuses on **agent workflows for real game-development tasks**. Its design takes inspiration from the experimental MCP/toolset and Agent Skill concepts in Unreal Engine 5.8, but is reimplemented for UE 5.5. Skills are served as Markdown `SKILL.md` files compatible with Claude Code and Codex.

## Core features

### Agent Skills

The plugin serves task guidance to connected agents. A skill consists of `SKILL.md` plus optional references and scripts, and project-specific skills can override the defaults that ship with the plugin.

Included skills:

- `umg-authoring`: production-oriented UMG authoring with reusable Widget Blueprint components, C++ `BindWidget` contracts, data-driven lists, Theme Data Assets and capture-based review
- `ui-style-system`: design tokens, a UI kit/gallery, style extraction and viewport-capture comparison
- `ui-art-requests`: an art request → review → import → connection workflow between UMG and an image model, another agent or an artist
- `blueprint-graphs`: Blueprint classes and logic: create a class, add variables, functions and event dispatchers, look node types up, add and wire nodes, compile once and check the behavior in a play session
- `perf-investigation`: performance work that ends in proof: fixed scenes played in a standalone game, marked moments for stalls, Insights hotspots, before/after comparison against run-to-run noise

### UMG-focused tools

The UMG toolset goes beyond creating Widget Blueprints.

- inspect Widget Trees and Named Slots
- inspect C++ parent classes and verify `BindWidget` / `BindWidgetOptional` contracts against the actual widget names and types
- create Widget Blueprints, add whole subtrees, and change widget and slot properties
- dry-run destructive edits and report the affected bindings and graph references before removal
- iterate through Blueprint compile → PIE → viewport capture → review

### Blueprint logic tools

Agents can build Blueprint logic without the graph editor. Node types are named as in the experimental Blueprint toolset of UE 5.8 (`Development|PrintString`), reimplemented on UE 5.5 editor APIs.

- create Blueprint classes and add variables, functions with inputs and outputs, and event dispatchers
- find node types as the editor's node menu offers them, optionally only those that connect to a given pin
- add, wire and remove nodes and set pin values in one call that is checked first, recorded as one undo step and rolled back when an operation fails
- read a large graph one event chain at a time, with each node's compile message
- verify through Blueprint compile → PIE → log

### Build → Run → Review

Blueprint and C++ compilation, Play In Editor, editor logs and viewport capture are available through the same MCP server so an agent can verify what it changed in the running editor.

```text
Inspect → Edit → Compile → PIE → Capture / Log → Review → Iterate
```

### Performance investigation

Profiling tools read what a profiled run of the game leaves behind, and the `perf-investigation` skill runs the game for them.

- summarize CSV profiler captures per scene: median of the run averages, spread between runs, 95th percentile, and the cost of each marked moment (`CSV_EVENT`) such as opening a screen or a drag and drop
- compare two sets of runs and flag only changes larger than the noise of both
- export an Unreal Insights trace without opening Insights and add up self and inclusive time per timer over each marked moment
- read a `ProfileGPU` dump from a game log as a tree of passes
- play the runs with a quiet-machine check, a warm-up launch, background-load sampling that replays disturbed runs, and the user settings file restored after each launch

```text
Scenes → Baseline runs → Mark moments → Trace → Cause → Fix → Candidate runs → Compare
```

In the Sonheim project the tools located and confirmed these UI fixes (1920×1080, median of the events):

| Moment | Before | After | Cause the trace named |
|---|---|---|---|
| Swapping two inventory items | 32.6 ms frame (19.4 ms game thread) | 16.5 ms (2.9 ms) | every slot cleared, refilled and its tooltip reattached on each change |
| Opening the inventory | 57.5 ms (44.6 ms) | 21.7 ms (8.1 ms) | the screen, 42 slots and a tooltip per item created on every opening |

### Reflection-based toolsets

Tools are plain `static UFUNCTION`s. Names, descriptions, arguments and result JSON schemas are generated from Unreal Reflection, so adding a function to a toolset creates an MCP tool.

### AI chat in the editor

A chat panel lets planners and artists ask about the project and have it changed, signed in with their own Claude, ChatGPT or Google account. Permission modes decide what the model may change, and every change waits for an approval card. See [AI chat](Docs/AiChat.md).

> **Status: beta.** Built and tested with Unreal Engine 5.5.4 (installed build) on Windows 64-bit. The smoke test in this repository passes 290 checks against the testbed project. The plugin also builds on Unreal Engine 5.7.4 and is used there in one project, where the smoke test has not been run yet. Other platforms have not been tried.

## Dungeon UI — workflow case study

`Content/Samples/DungeonUi` contains a dungeon progress HUD and result popup built by Claude Code **without opening the UMG Designer**, using Agent MCP tools and skills.

The first version was a flat tree of 48 and 78 widgets per screen. It worked, but repeated elements were copied, style values were scattered inline, and the reward list could not grow from data.

After review, the production rules were moved into the `umg-authoring` skill and the UI was rebuilt around reusable components and data:

```text
Flat Widget Tree
      ↓ review
Reusable Widget Blueprint Components
+ C++ BindWidget Contracts
+ DynamicEntryBox
      ↓ review
Theme Data Asset
+ DataTable
+ Art Request Pipeline
+ Capture-based Review
```

![Dungeon result popup](Docs/Images/dungeon_result.jpg)

![Dungeon progress HUD](Docs/Images/dungeon_hud.jpg)

The [Dungeon UI case study](Docs/Samples/DungeonUi.md) records the rejected versions, the problems they revealed, and how those findings changed both the sample and the plugin workflow.

Its look is a small design system: color and radius tokens and named box, text, bar and button styles in a theme data asset, styled widgets that store only a style name, and a kit gallery that shows every token and component. [UI style system](Docs/UiStyleSystem.md) describes how an agent extracts, applies and checks a style with the scripts of the `ui-style-system` skill.

## Documentation

| Page | Contents |
|---|---|
| [Connecting a client](Docs/Clients.md) | Claude Code and Codex configuration, ports for several editors |
| [Tools](Docs/Tools.md) | Every tool with its access level, and typical workflows |
| [AI chat](Docs/AiChat.md) | The editor's chat panel: providers, sign-in, permission modes, approvals, conversations |
| [Skills](Docs/Skills.md) | Served `SKILL.md` guides and where they are read from |
| [Safety](Docs/Safety.md) | What the server refuses, and the approval gate for tagged clients |
| [Settings](Docs/Settings.md) | Project settings of the server and the tools |
| [Writing tools](Docs/WritingTools.md) | Adding a toolset in C++ |
| [Testbed and smoke test](Docs/Testing.md) | The testbed project, `mcp_smoke.py` and `mcp_call.py` |
| [Dungeon UI sample](Docs/Samples/DungeonUi.md), [UI style system](Docs/UiStyleSystem.md) | The case study above in detail |

## Installation

1. Copy `Plugins/AgentMcp` into the `Plugins` folder of your project.
2. Enable the plugin in **Edit > Plugins**, or in the `.uproject` file:
   ```json
   "Plugins": [ { "Name": "AgentMcp", "Enabled": true } ]
   ```
3. Build the editor target of your project (the plugin ships as source code) and open the editor.

When the editor has loaded, the output log shows:

```
LogAgentMcpProtocol: Agent MCP server listening on http://127.0.0.1:18765/mcp (39 tools).
```

The editor then writes `.mcp.json` and `.codex/config.toml` into the project folder with that endpoint, so Claude Code finds the
editor with no further setup. Codex reads a project file only in a folder it trusts; to reach every folder instead, turn on
**Write User Codex Config** in Project Settings or call `editor_write_client_config` with `bUserCodexConfig`, which points the entry of the user's
`~/.codex/config.toml` at this editor and keeps a copy of the file before its first change. While `AuthToken` is set the editor
leaves the project files alone, because they are often committed and would carry the token; add the header by hand as below.

The editor writes `.mcp.json` for Claude Code and `.codex/config.toml` for Codex into the project folder on startup. To write them by hand, use a token, or run several editors at once, see [Connecting a client](Docs/Clients.md). Agents should start with `editor_get_state`.

## Limitations

- Tested with Unreal Engine 5.5.4 on Windows 64-bit, and used with Unreal Engine 5.7.4 on Windows 64-bit.
- Major inspection, UMG, compilation, PIE, save, skill, DataTable and asset tools were used from Claude Code 2.1.270 while building the UI sample.
- Codex has been used through the AI chat panel; a direct connection through `.codex/config.toml` has not been tested.
- Responses are plain JSON. There is no SSE or progress streaming.
- Requests execute on the editor game thread. **Use Less CPU when in Background** can make calls noticeably slower when the editor is unfocused.
- `livecoding_compile` blocks until compilation completes, and Live Coding cannot apply reflected declaration changes to `UCLASS`, `USTRUCT`, `UPROPERTY` or `UFUNCTION`.
- Meshes are imported with the materials and textures their file carries, and there is no tool to author materials. Fab content is added through the Fab plugin, which needs the user's Epic account.
- The level tools create and open levels and place actors. World Partition data layers, streaming sources and level instances are not covered, and a partitioned level saves its actors in their own packages.
- Source control handling of `level_save` has not been tested.
- Blueprint variables, functions and event dispatchers can be added but not yet removed or renamed; local variables, function overrides and interfaces are not covered, and graphs are not laid out automatically. In a localized editor the node type ids of variables and event dispatchers carry localized categories. Widget animations and designer property bindings can be inspected but not edited, and widgets cannot yet be moved to a different parent or changed to another class; the UI sample rebuilt subtrees instead.
- `ToolSearch` exposure mode and source-control handling on save have not yet been tested.

## Background

The toolset layout and reflection-based design take inspiration from the experimental Model Context Protocol/toolset plugins shipped by Epic Games with Unreal Engine 5.8 and were reimplemented for Unreal Engine 5.5. The Agent Skill concept was also an influence, but this project does not port `UAgentSkill`; it implements a separate Markdown `SKILL.md` system that can be shared with Claude Code and Codex.

This repository contains no source files from Epic's plugins. Unreal and Unreal Engine are trademarks or registered trademarks of Epic Games, Inc.

## License

[MIT License](LICENSE).
