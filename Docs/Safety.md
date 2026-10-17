# Safety

[한국어](Safety.ko.md)

- **Local only.** The server listens on `127.0.0.1`. Requests whose browser `Origin` is not `localhost`, `127.0.0.1` or `[::1]` are refused. `AuthToken` additionally requires a bearer token.
- **Undoable, all-or-nothing writes.** Write tools validate values before changing anything and run in an editor transaction. If a tool fails after changing state, the transaction is rolled back.
- **No writes during play.** Write tools and asset creation/import tools are refused while Play In Editor is starting or running.
- **Project content only.** Assets can be created, imported, changed and saved only under `/Game` and project plugins. Engine content is read-only.
- **Explicit saving.** No tool saves as a side effect, with one exception: `level_new` writes the level it creates, because a level only exists on disk. `asset_save` saves loaded project assets only and still refuses levels; levels are saved with `level_save`, which writes the level open in the editor.
- **No level is replaced silently.** `level_new` and `level_open` refuse to run while a level has unsaved changes, because opening a level discards them.
- **Dry runs.** Destructive tools need `bConfirm: true` to act, and `asset_import_textures` validates every entry before importing anything.
- **Allow and block lists.** `AllowedTools` and `BlockedTools` hide tools, while `BlockedProperties` protects properties from `object_set_properties` and UMG tools.
- **Files.** `skills_get` can read only inside a skill folder and refuses outside paths and hidden files.
- **Source files.** The source tools read and write text files only inside `SourceFolders` and refuse other paths, `..` that climbs out, build folders and files that are no text. Their edits are not undoable from the editor.
- **Approval for tagged clients.** A session that sends the `X-AgentMcp-Client` header keeps that tag for its lifetime. Every tool call of a tagged session other than a Read tool waits for the approval gate registered for the tag; with no gate registered it fails with `APPROVAL_UNAVAILABLE`, and a refusal returns `DENIED`. The [AI chat](AiChat.md) registers the gate `agent-mcp-chat` and shows an approval card for each change. Sessions without the header are not affected, so Claude Code and Codex connected directly work as before.
- **Perf files.** The perf tools read files only inside the project folder. `perf_trace_hotspots` starts the engine's own `UnrealInsights.exe` to export a trace next to it.
- There is no tool that runs console commands or arbitrary scripts.
