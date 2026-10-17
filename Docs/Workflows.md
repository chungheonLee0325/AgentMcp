# Task-oriented MCP workflows

[한국어](Workflows.ko.md) · [Documentation index](Index.md)

These examples show **the order in which to use tools**, not shell commands. The named calls are MCP tools invoked by Claude Code or Codex connected to the editor. See the [tool reference](Tools.md) for argument schemas and access levels.

Before making any change, call `editor_get_state` to confirm the **project, current level, PIE state, and unsaved changes**.

## 1. Find the impact of a Blueprint change

```text
editor_get_state
  → asset_find / blueprint_inspect
  → blueprint_find_usages
  → blueprint_get_graph or blueprint_read_graph
  → review affected callers and graphs
```

- `blueprint_find_usages` queries the Find in Blueprints index for function calls, variable reads/writes, casts, and event bindings, including Blueprints that are not currently loaded.
- For large graphs, use the entry-point or connected-chain options of `blueprint_get_graph`, or the compact representation from `blueprint_read_graph`.
- Inspect the targets before making edits rather than treating search results alone as the full context.

## 2. Author and review a Widget Blueprint

```text
umg_inspect (for existing widgets)
  → umg_create_widget_blueprint
  → umg_add_widgets / umg_set_widget_properties
  → blueprint_compile
  → pie_start
  → viewport_capture
  → log_get_recent
  → pie_stop
  → asset_save
```

- Before deleting an existing subtree, use the `umg_remove_widgets` dry run and review affected references, then pass `bConfirm` when ready.
- With a C++ widget parent, check actual `BindWidget` / `BindWidgetOptional` names and types using `umg_inspect`.
- Prefer reusable Widget Blueprint components and data-driven collections over copied subtrees. See the [Dungeon UI case study](Samples/DungeonUi.md) and the `umg-authoring` Skill.
- Compile success alone is not a visual review: inspect the PIE capture and logs.

## 3. Edit Blueprint logic

```text
blueprint_inspect
  → blueprint_get_graph
  → blueprint_find_node_types
  → blueprint_add_members (if needed)
  → blueprint_edit_graph
  → blueprint_compile
  → pie_start → log_get_recent → pie_stop
  → asset_save
```

- Find real editor node types and compatible pins with `blueprint_find_node_types` rather than guessing type IDs.
- A graph-edit call is a single Undo step and rolls back its operations if that call fails.
- Adding variables, functions, and event dispatchers is supported; several removal, rename, and automatic-layout operations remain out of scope.

## 4. Inspect and modify C++ or configuration

```text
source_find / source_search
  → source_read
  → review intended change
  → source_replace
  → livecoding_compile (where applicable)
  → log_get_recent
```

- Source tools operate on text files in configured `SourceFolders` and support UTF-8, UTF-16, and CP949.
- `source_replace` and `source_write` are **not Unreal Editor Undo transactions**. Check source control before changing files.
- Changes to reflected `UCLASS`, `USTRUCT`, `UPROPERTY`, or `UFUNCTION` declarations require a full build rather than Live Coding.

## 5. Inspect assets and show them in the editor

```text
asset_find / actor_find
  → asset_inspect / actor_inspect
  → editor_show_in_content_browser or editor_open_assets
  → actor_select or viewport_set_camera
```

These inspection and navigation calls do not edit project content. To change an asset value, first check available properties using `object_list_properties` / `object_get_properties`, then use `object_set_properties`.

## 6. Measure and compare performance

```text
Close the editor
  → Run fixed scenes repeatedly using perf-investigation Skill's run.py
  → perf_summarize_csv (CSV captures)
  → perf_trace_hotspots (Insights trace, if needed)
  → perf_read_gpu_profile (ProfileGPU log, if needed)
  → change code or assets
  → replay the same scenes and conditions
  → perf_compare_csv
```

- The `perf-investigation` Skill guides standalone-game runs, warm-up, machine-load checks, and replaying disturbed runs.
- `perf_summarize_csv` summarizes run sets and marked events; `perf_compare_csv` compares changes against run-to-run variation.
- Mark significant moments using `CSV_EVENT` and inspect per-timer costs in an Insights trace when investigating causes.
- Record the scene, resolution, build, and run conditions alongside measurements. A single differing run is not sufficient evidence of improvement.

## Direct clients versus in-editor AI chat

[In-editor AI chat](AiChat.md) uses permission modes and approval cards. The same cards are **not automatically applied** to a directly connected Claude Code or Codex client. See [Connecting clients](Clients.md) and [Safety](Safety.md) for the differences.
