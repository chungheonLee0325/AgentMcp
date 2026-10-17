# Agent Skills — task guidance served by the editor

[한국어](Skills.ko.md) · [All tools](Tools.md) · [Workflows](Workflows.md)

**Tools expose operations in Unreal Editor; Skills define how to approach a task using those operations.** AgentMcp serves Skills through the editor's MCP server instead of locking instructions to Claude Code. Inspired by Unreal Engine 5.8's Agent Skill concept, it provides a separate Markdown-based implementation for earlier engine versions.

## Why separate Tools and Skills?

The first Dungeon UI sample could be authored through Widget Blueprint tools, but it duplicated repeated elements and embedded style values in individual widgets. **Being able to create a widget tree does not decide whether that tree is maintainable.**

The UMG tools therefore handle *inspecting and changing Widget Trees*, while the `umg-authoring` Skill describes *reusable components, C++ `BindWidget` contracts, data-driven lists, and in-game visual review*.

| Layer | Responsibility | Example |
|---|---|---|
| MCP Tool | Inspect or modify actual project state | `umg_inspect`, `umg_add_widgets`, `blueprint_compile`, `viewport_capture` |
| Agent Skill | Guide the order of investigation, authoring and review | Find existing components first; use reusable entry widgets for repeated lists |
| Project configuration | Replace built-in guidance with team conventions | Provide a project-level Skill named `umg-authoring` |

[How the Dungeon UI work separated Tools and Skills](Samples/DungeonUi.md#3-separating-tools-from-skills)

## How an agent uses a Skill

```text
Agent connects to the editor's MCP server
  → Reads available Skill names and descriptions in server instructions
  → Requests a matching Skill with skills_get
  → Reads SKILL.md and any needed supporting files
  → Invokes actual MCP Tools following those instructions
  → Reviews the result through compilation, PIE and captures
```

For instance, before authoring game UI:

```json
{"name": "umg-authoring"}
```

Call `skills_get` with these arguments to receive the `SKILL.md` body (without YAML front matter) and a list of supporting files. To read an additional file:

```json
{"name": "perf-investigation", "file": "references/scene-runner.md"}
```

- `skills_list`: Returns currently discovered names, descriptions, paths, overridden paths and reasons for skipped files.
- `skills_get`: Reads a Skill body or a text file within that Skill folder. `files` lists other available paths; `bTruncated` indicates truncated output.
- A Skill is **guidance for selecting and using tools**. Reading it does not create or edit assets by itself.

## Included Skills

| Skill | Scope |
|---|---|
| [`umg-authoring`](../Plugins/AgentMcp/Skills/umg-authoring/SKILL.md) | Reusable Widget Blueprints, `BindWidget`, data-driven lists, visual review in PIE |
| [`ui-style-system`](../Plugins/AgentMcp/Skills/ui-style-system/SKILL.md) | Theme Data Assets, style tokens, extraction and application, UI Kit Gallery |
| [`ui-art-requests`](../Plugins/AgentMcp/Skills/ui-art-requests/SKILL.md) | Art requests and review, texture import and UMG integration |
| [`blueprint-graphs`](../Plugins/AgentMcp/Skills/blueprint-graphs/SKILL.md) | Blueprint class, member and graph edits, runtime checks |
| [`perf-investigation`](../Plugins/AgentMcp/Skills/perf-investigation/SKILL.md) | Repeated fixed-scene runs, CSV and Insights analysis, before/after comparisons |

The performance Skill includes scripts for measuring a standalone game **with the editor closed**, as well as a scene-runner reference. **Scripts execute the measurements; MCP performance tools interpret the generated files.** [Performance workflow](Workflows.md#6-measure-and-compare-performance)

## Add or customize project Skills

Skills are searched in this order. **A later Skill with the same name replaces the earlier one completely; their instructions are not merged.**

1. `Plugins/AgentMcp/Skills` — built-in plugin Skills
2. `<project>/AgentMcp/Skills` — project-specific Skills
3. `SkillDirectories` — additional configured locations, in the configured order. Relative paths are resolved from the project root.

To add a project-specific Skill, create `AgentMcp/Skills/studio-ui/SKILL.md`:

```markdown
---
name: studio-ui
description: Apply this project's UI naming and review rules when editing UMG screens.
---

# Studio UI

1. Inspect existing Widget Blueprints and their C++ parent classes first.
2. Follow this project's naming and component conventions.
3. Compile changes and review the actual screen in PIE.
```

The folder and `name` must match. Names use lowercase ASCII letters, digits and hyphens. The `description` tells agents when the guidance applies. Additional roots can be configured through [`SkillDirectories`](Settings.md).

**To see dynamic loading:** Use `skills_list` to find `studio-ui`, read it with `skills_get`, edit its `SKILL.md` **without restarting Unreal Editor**, and call the same `skills_get` again. The updated body is returned. To override the built-in `umg-authoring` Skill instead, create `AgentMcp/Skills/umg-authoring/SKILL.md` with matching front matter.

**Discovery has two moments.** `skills_list` and `skills_get` rescan directories and read files on each request. However, the **Skill list embedded in the initial MCP server instructions is generated during initialization**. Newly added names and descriptions may require an MCP client reconnection to appear in those initial instructions.

## Discovering Skills from Claude Code and Codex

**Skills advertised by the server** are distinct from **Skills discovered locally by an agent client**. This repository provides small bridge files carrying each built-in Skill's name and description:

- Claude Code: [`.claude/skills`](../.claude/skills)
- Codex: [`.agents/skills`](../.agents/skills)

When the client selects one of those local bridges, it is instructed to call `skills_get`. The actual instructions remain in **one plugin-owned `SKILL.md`**. Those bridge files are not automatically installed in other projects or clients; add the appropriate bridge if that discovery behavior is desired.

## Implementation

| Source | Responsibility |
|---|---|
| [`AgentMcpSkills.cpp`](../Plugins/AgentMcp/Source/AgentMcpToolset/Private/AgentMcpSkills.cpp) | `ScanSkills` searches locations in order; `ParseSkillFile` parses front matter and body |
| [`AgentMcpSkillTools.cpp`](../Plugins/AgentMcp/Source/AgentMcpTools/Private/Toolsets/AgentMcpSkillTools.cpp) | `skills_list` / `skills_get` and on-demand file reads |
| [`AgentMcpSkillTools.h`](../Plugins/AgentMcp/Source/AgentMcpTools/Private/Toolsets/AgentMcpSkillTools.h) | Reflected MCP Tool declarations and structured results |
| [`AgentMcpSettings.h`](../Plugins/AgentMcp/Source/AgentMcpToolset/Public/AgentMcpSettings.h) | `SkillDirectories` and result-size settings |

`skills_get` serves text files only from within the selected Skill directory. Parent-directory traversal via `..`, hidden files and binary content are rejected; responses have size limits.

Unreal Engine 5.8 exposes an asset/class-based Skill interface through `UAgentSkill` and `UAgentSkillToolset`. AgentMcp instead implements **discovery and loading of Markdown files**. It does not port the UE 5.8 Skill classes or claim to expose the same API. [UE 5.8 `UAgentSkill`](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ToolsetRegistry/UAgentSkill) · [`UAgentSkillToolset`](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ToolsetRegistry/UAgentSkillToolset)
