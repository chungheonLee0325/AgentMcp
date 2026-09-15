# Agent MCP for Unreal Engine 5.5

[한국어](README.ko.md)

Agent MCP runs a [Model Context Protocol](https://modelcontextprotocol.io) server inside the Unreal Editor, so that coding
agents such as Claude Code can investigate a project, change it, run it and check the result:

- inspect levels, actors, properties, assets, Blueprints, Widget Blueprints and DataTables
- change actors, data assets and other project assets, DataTable rows and widget trees in undoable editor transactions
- create Widget Blueprints, data assets and DataTables, import textures, and save assets explicitly
- compile Blueprints, compile C++ with Live Coding, start and stop Play In Editor
- read the editor log and capture the viewport, including the game UI of a play session
- serve skills: task guides, such as how to build game UI with these tools, that every connected agent reads in the same version

Tools are plain `static UFUNCTION`s. Their names, descriptions and JSON schemas come from reflection, so a new tool is one
function.

> **Status: beta.** Built and tested with Unreal Engine 5.5.4 (installed build) on Windows 64-bit. The smoke test in this
> repository passes 177 checks against the testbed project. Other engine versions and platforms have not been tried.

- [Installation](#installation)
- [Connecting a client](#connecting-a-client)
- [Tools](#tools)
- [Skills](#skills)
- [Safety](#safety)
- [Settings](#settings)
- [Writing tools](#writing-tools)
- [Testbed and smoke test](#testbed-and-smoke-test)
- [UI sample](#ui-sample)
- [Limitations](#limitations)
- [License](#license)

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

## Connecting a client

The server speaks MCP Streamable HTTP with JSON responses at `http://127.0.0.1:18765/mcp`.

For Claude Code, add a `.mcp.json` file to the project root:

```json
{
  "mcpServers": {
    "unreal": { "type": "http", "url": "http://127.0.0.1:18765/mcp" }
  }
}
```

If `AuthToken` is set, add `"headers": { "Authorization": "Bearer <token>" }` to the server entry.

For Codex, add a `.codex/config.toml` file to the project root. Codex reads it only in a trusted project, one that
`~/.codex/config.toml` lists with `trust_level = "trusted"` after you trust the folder.

```toml
[mcp_servers.unreal]
url = "http://127.0.0.1:18765/mcp"
tool_timeout_sec = 600
```

`tool_timeout_sec` raises the Codex default of 60 seconds, which compiling, saving and play sessions can exceed. If `AuthToken` is
set, add `http_headers = { Authorization = "Bearer <token>" }`, or name an environment variable that holds the token with
`bearer_token_env_var`. Start a new Codex session after changing the file.

The server returns short usage instructions from `initialize`, including the list of skills. Agents should start with
`editor_get_state`.

**One port per editor.** Every editor that enables the plugin uses the configured port. When two editors run at the same
time, the second one cannot bind the port, logs `Agent MCP server failed to start`, and serves no tools. Give projects that
run at the same time different ports, and check the `project` field of `editor_get_state` before changing anything.

## Tools

**Read** tools have no side effects. **Write** tools run in an undoable editor transaction and are refused during Play In
Editor. **Destructive** tools are Write tools that only report what they would do unless `bConfirm` is true. **Control**
tools change editor state that cannot be undone: play sessions, compiling, saving, and creating or importing assets.

| Tool | Access | Description |
|---|---|---|
| `editor_get_state` | Read | Open level, play session, unsaved packages, selection, undo history and server state |
| `editor_undo`, `editor_redo` | Control | Undo or redo the last editor transaction |
| `log_get_recent` | Read | Editor log lines after a sequence number, filtered by verbosity, category or text |
| `actor_find` | Read | Actors by label or name, class, tag, folder or selection, in the editor or play world |
| `actor_inspect` | Read | Class, Blueprint, tags, transform, attachment and components of an actor |
| `actor_set_transform` | Write | Move, rotate or scale an actor |
| `object_list_properties` | Read | Properties of an object and whether they can be changed |
| `object_get_properties` | Read | Property values as JSON |
| `object_set_properties` | Write | Change properties of an actor or component in the editor level, or of a project asset such as a data asset or texture, with readback |
| `pie_start`, `pie_stop` | Control | Start or stop Play In Editor and wait until the session has begun or shut down |
| `pie_status` | Read | Whether a play session is starting or running |
| `asset_find` | Read | Assets by folder, class and name, without loading them |
| `asset_inspect` | Read | Class, tags, file size, loaded and dirty state and reference counts of an asset |
| `asset_referencers`, `asset_dependencies` | Read | Packages that reference an asset, or that it depends on |
| `asset_save` | Control | Save loaded project assets without dialogs |
| `asset_create` | Control | Create a data asset or a DataTable |
| `asset_import_textures` | Control | Import PNG, JPEG, TGA or BMP files as textures, with the settings for UMG |
| `class_find_derived` | Read | C++ and Blueprint classes that derive from a class, with headers and assets |
| `datatable_get_schema` | Read | Row struct, C++ header, columns, C++ types and JSON schemas |
| `datatable_list_rows`, `datatable_get_rows` | Read | Row names, and values keyed by row and column |
| `datatable_set_rows`, `datatable_add_rows`, `datatable_rename_rows` | Write | Change, add or rename rows of a project DataTable |
| `datatable_remove_rows` | Destructive | Remove rows of a project DataTable |
| `blueprint_inspect` | Read | Parent chain, interfaces, components, variables, functions and graphs |
| `blueprint_compile` | Control | Compile a Blueprint or Widget Blueprint and return its errors and warnings |
| `umg_inspect` | Read | Widget tree with slots, BindWidget properties, animations and property bindings |
| `umg_create_widget_blueprint` | Control | Create a Widget Blueprint with a parent class and a root panel |
| `umg_add_widgets` | Write | Add widgets, whole subtrees or Widget Blueprint instances, with widget and slot properties, in one call |
| `umg_set_widget_properties` | Write | Change widget properties, slot values and the variable flag of several widgets |
| `umg_remove_widgets` | Destructive | Remove widgets with their descendants, property bindings and graph references |
| `viewport_capture` | Read | PNG of the level viewport or the play session, including the game UI |
| `livecoding_compile` | Control | Compile changed C++ with Live Coding and wait for the result |
| `skills_list` | Read | Skills of the plugin and the project with their descriptions, and skill files that were skipped |
| `skills_get` | Read | Instructions of a skill, or one of its other files |

A typical verification loop: `blueprint_compile` → `pie_start` → `log_get_recent` from the returned `startLogSequence` →
`viewport_capture` → `pie_stop`.

To build a user interface: `umg_create_widget_blueprint` (with a C++ parent class that declares `BindWidget` properties) →
`umg_add_widgets` → `blueprint_compile` → look at it in a play session with `viewport_capture` → adjust it with
`umg_set_widget_properties`. Values are JSON: property names are the C++ names (`Text`, `Font`, `Padding`, `LayoutData`),
a struct value may list only the fields to set, and enum values are names (`HAlign_Center`, `RoundedBox`). An entry class can
be a Widget Blueprint, whose instance properties are set the same way. The edit tools read back only the requested fields;
`umg_inspect` with `bIncludeProperties` returns complete values.

To keep the look of a UI in data: `asset_create` makes a theme data asset or an item DataTable, `object_set_properties` and the
datatable tools fill them, and `asset_import_textures` brings in icons and frames with the texture settings for UMG.

The tools build whatever tree they are given. How a UI team would build it (reusable component Widget Blueprints, style values in
one place, data-driven lists and a capture review) is described by the plugin's skill `umg-authoring`. How images are requested from
an image model, another agent or an artist, checked on a review sheet and connected is described by `ui-art-requests`. How the
style of a project is extracted from its Widget Blueprints or a mockup, kept as design tokens and a kit gallery, and checked with
capture comparisons is described by `ui-style-system`; see [Skills](#skills).

## Skills

A skill is a task guide for agents: a folder with a `SKILL.md` file whose front matter has a `name` and a `description`, the format
that Claude Code and Codex use for their own skills. The editor serves the skills, so every connected agent reads the same version,
and the skills travel with the plugin.

- The server instructions returned by `initialize` list each skill with its description.
- `skills_list` returns the skills with their folders, and the skill files that were skipped with the reason.
- `skills_get` returns the instructions and the folder of a skill and, on request, its other files, such as references or examples.
  Scripts of a skill, such as the review sheet of `ui-art-requests`, run from that folder.

Skills are read from these folders in this order. A skill replaces one with the same name from an earlier folder, so a project can
adapt a plugin skill.

1. `Plugins/AgentMcp/Skills`: the skills of the plugin, currently `umg-authoring`, `ui-art-requests` and `ui-style-system`
2. `AgentMcp/Skills` in the project folder
3. the folders of the `SkillDirectories` setting

The files are read on every call, so a changed skill applies without restarting the editor. Only the list in the server
instructions is made when the server starts.

Claude Code and Codex choose skills by their descriptions. To let them start a served skill on their own, add a short `SKILL.md`
with the same name and description to `.claude/skills/<name>/` for Claude Code or `.agents/skills/<name>/` for Codex that tells the
agent to call `skills_get`. This repository has both for the plugin skills.

Unreal Engine 5.8 serves skills the same way: skills are `UAgentSkill` classes defined in C++, Python or Blueprint, read through
`ListSkills` and `GetSkills` tools. Agent MCP reads Markdown files instead, so a skill is edited as text and has the same format as
the skills of Claude Code and Codex.

## Safety

- **Local only.** The server listens on `127.0.0.1`. Requests whose browser `Origin` is not `localhost`, `127.0.0.1` or
  `[::1]` are refused, which blocks web pages from reaching the editor. `AuthToken` additionally requires a bearer token.
- **Undoable, all-or-nothing writes.** Write tools validate every value before they change anything and run in an editor
  transaction. If a tool fails after it has changed something, the transaction is undone.
- **No writes during play.** Write tools and the tools that create or import assets are refused while Play In Editor is starting
  or running.
- **Project content only.** Tools create, import, change and save assets under `/Game` and in project plugins; engine content is
  read-only. `object_set_properties` also refuses Blueprints and DataTables, which have their own tools, and levels that are not open.
- **Explicit saving.** No tool saves as a side effect. `asset_save` saves loaded assets of the project only; levels are refused.
- **Dry runs.** Destructive tools need `bConfirm: true` to act, and `asset_import_textures` checks every entry before it imports
  anything.
- **Allow and block lists.** `AllowedTools` and `BlockedTools` hide tools, and `BlockedProperties` protects properties from
  `object_set_properties` and the `umg` tools.
- **Files.** `skills_get` reads files inside a skill folder only; other paths and hidden files are refused.
  `asset_import_textures` reads the image files it is given, also outside the project folder.
- There is no tool that runs console commands or scripts.

## Settings

Set values in `Config/DefaultEditorPerProjectUserSettings.ini` of the project:

```ini
[/Script/AgentMcpToolset.AgentMcpSettings]
Port=18765
+BlockedTools=datatable_remove_rows
```

| Key | Default | Meaning |
|---|---|---|
| `bAutoStartServer` | `True` | Start the server when the editor has loaded |
| `Port` | `18765` | Loopback port |
| `UrlPath` | `/mcp` | Endpoint path |
| `AuthToken` | empty | When set, clients must send `Authorization: Bearer <token>` |
| `ExposureMode` | `Native` | `Native` registers every tool. `ToolSearch` registers only `toolsets_list`, `toolsets_describe` and `tools_call` |
| `BlockedTools`, `AllowedTools` | empty | Tool name wildcards (for example `datatable_*`) |
| `bAllowWritesDuringPIE` | `False` | Allow Write tools during Play In Editor |
| `BlockedProperties` | empty | `ClassName.PropertyName` wildcards that `object_set_properties` and the `umg` tools refuse |
| `BusyWaitTimeoutSeconds` | `10` | How long a call waits while the editor saves, collects garbage or loads assets |
| `MaxResultBytes` | `65536` | Result text above this size is truncated |
| `LogBufferLines` | `20000` | Log lines kept for `log_get_recent` |
| `SkillDirectories` | empty | More skill folders, searched after the plugin's and the project's; relative paths start at the project folder |

## Writing tools

Add `AgentMcpToolset` to the dependencies of an editor module, then declare static functions on a `UAgentMcpToolset`
subclass:

```cpp
#include "AgentMcpToolset.h"

#include "MyTools.generated.h"

USTRUCT(BlueprintType)
struct FMyGreeting
{
	GENERATED_BODY()

	UPROPERTY()
	FString Message;
};

/** Example tools. */
UCLASS(meta = (McpToolset = "my"))
class UMyTools : public UAgentMcpToolset
{
	GENERATED_BODY()

public:
	/**
	 * Greets someone.
	 * @param Name Who to greet.
	 * @return The greeting.
	 */
	UFUNCTION(BlueprintCallable, Category = "My Tools", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FMyGreeting Greet(const FString& Name = TEXT("world"));
};
```

This becomes the tool `my_greet` with an optional string argument `name`.

- Toolset classes are found automatically. The tool name is `<McpToolset>_<function name in snake_case>`, and the comment
  provides the description.
- `McpAccess` is `Read`, `Write`, `Destructive` or `Control`. A missing value is treated as `Write`.
- Keep `BlueprintCallable`: Unreal Engine 5.5 records C++ default argument values only for Blueprint-callable functions, and
  without them every argument becomes required. `BlueprintInternalUseOnly` keeps the function out of Blueprint menus.
- Return a `USTRUCT(BlueprintType)`. Its fields become the JSON result; empty strings, arrays and structs are left out.
- Report a failure with `UE::AgentMcp::RaiseToolError(TEXT("CODE"), TEXT("Message"), TEXT("Hint"))` and return.
- Object arguments (`UObject*`, `AActor*`, `UClass*`) accept object paths, and actors also accept their labels.
- For work that finishes on a later frame, return `UAgentMcpAsyncResult::Create(TimeoutSeconds, PollFunction)`. Only Read
  and Control tools can do this.
- To return an image, add an `FAgentMcpImage` field to the result struct.

## Testbed and smoke test

The repository root is a small Unreal Engine 5.5 project that builds and tests the plugin.

| Path | Contents |
|---|---|
| `Plugins/AgentMcp` | The plugin, with its skills in `Plugins/AgentMcp/Skills` |
| `Source/AgentMcpTestbed` | Row struct, data asset class, widget base class and game mode used by the tests, and the C++ classes of the UI sample |
| `Source/AgentMcpTestbedEditor` | `testbed_*` tools that create test assets under `/Game/AgentMcpFixtures`, hooks for rollback and cancellation checks, and `sample_show_widget` |
| `Content/Samples/DungeonUi` | Widget Blueprints, theme data asset and item table of the UI sample, built with the tools |
| `Art/Requests` | Art requests of the samples |
| `Docs` | How the samples were built, and a planned experiment |
| `.mcp.json`, `.codex/config.toml` | Connect Claude Code and Codex to the testbed editor on port 18766 |
| `.claude/skills`, `.agents/skills` | Short skill files that let Claude Code and Codex start the plugin skills |
| `Config` | The testbed serves port **18766**, so that it never answers in place of another project on the default port, and adds the smoke test's skill folder to `SkillDirectories`. `DefaultGame.ini` selects the theme of the UI sample |
| `Tools/mcp_smoke.py` | Smoke test (Python 3, standard library only) |
| `Tools/mcp_call.py` | Calls one tool from the command line |

1. Build the `AgentMcpTestbedEditor` target:
   `<UE>\Engine\Build\BatchFiles\Build.bat AgentMcpTestbedEditor Win64 Development -Project=<path>\AgentMcpTestbed.uproject -WaitMutex`
2. Open `AgentMcpTestbed.uproject` and wait for `Agent MCP server listening on http://127.0.0.1:18766/mcp`.
3. Run `python Tools/mcp_smoke.py --out Saved/MCP/smoke.json`.

The smoke test first checks that the editor behind the URL is the `AgentMcpTestbed` project and stops otherwise, because
it starts Play In Editor, changes the level and saves the test assets. It covers the MCP transport and its errors, every
tool, undo and rollback, request cancellation, Play In Editor, viewport capture with the game UI, Live Coding, Widget
Blueprint editing including nested Widget Blueprint instances, skills, creating data assets and DataTables, and importing textures.

To call a single tool:

```
python Tools/mcp_call.py editor_get_state --url http://127.0.0.1:18766/mcp --expect-project AgentMcpTestbed
```

## UI sample

`Content/Samples/DungeonUi` holds a dungeon progress HUD and a dungeon result popup that Claude Code built through the tools:
component Widget Blueprints for reward slots, stat tiles and objective rows, C++ bases with `BindWidget` contracts and intro
animations, a `DynamicEntryBox` that creates one reward slot per reward, a theme data asset for colors and frames, and an item
table. The icons and frames it still draws as shapes are listed in an art request. [Docs/Samples/DungeonUi.md](Docs/Samples/DungeonUi.md)
describes how it was built, including the versions that reviews sent back, and how to run it.

![Dungeon result popup](Docs/Images/dungeon_result.jpg)

## Limitations

- Tested with Unreal Engine 5.5.4 on Windows 64-bit only. All tools have been tested with the Python client in `Tools`.
  From Claude Code 2.1.270, `editor_get_state`, `actor_find` and `viewport_capture` were called from the desktop app and the
  CLI. While building the UI sample, the desktop app also called `umg_create_widget_blueprint`, `umg_add_widgets`,
  `umg_set_widget_properties`, `umg_remove_widgets`, `umg_inspect`, `blueprint_compile`, `pie_start`, `pie_stop`, `asset_save`,
  `livecoding_compile`, `skills_list`, `skills_get`, `asset_create`, `datatable_add_rows`, `object_get_properties`,
  `object_set_properties` and `editor_undo`. The other tools have not been called from Claude Code yet.
- The Codex setup (`.codex/config.toml`, `.agents/skills`) follows the Codex documentation and has not been tested with Codex yet.
- Responses are plain JSON. There is no streaming: no SSE and no progress notifications. `pie_start` and similar tools hold
  the request until they finish.
- Requests run on the editor's game thread. An editor in the background with **Use Less CPU when in Background** enabled
  ticks about three times per second, so each call then takes about a third of a second. Disable that editor preference
  while an agent works.
- `livecoding_compile` blocks the editor until the compile has finished, and Live Coding cannot apply changes to `UCLASS`,
  `USTRUCT`, `UPROPERTY` or `UFUNCTION` declarations. Close the editor and build instead.
- Tools change DataTable rows, the widget trees of Widget Blueprints and the properties of data assets and other project assets,
  and create data assets, DataTables and textures. Blueprint graphs and class defaults, widget animations and designer property
  bindings can be inspected but not edited, and a widget cannot be moved to another parent yet.
- The `ToolSearch` exposure mode and source control handling when saving have not been tested yet.

## Background

The tool set and its reflection-based design follow the experimental Model Context Protocol and toolset plugins that Epic
Games ships with Unreal Engine 5.8, reimplemented for Unreal Engine 5.5. This repository contains no source files from those
plugins. Unreal and Unreal Engine are trademarks or registered trademarks of Epic Games, Inc.

## License

[MIT License](LICENSE).
