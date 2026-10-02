# Agent MCP

An Unreal Engine 5.5 plugin that serves MCP tools from inside the editor, plus the testbed project it is developed in.
`AgentMcpTestbed.uproject` at the repository root is that project; the plugin lives in `Plugins/AgentMcp`.

## Editors and ports

| Editor | Port | Where the port is set |
|---|---|---|
| AgentMcpTestbed (this repo) | 18766 | `Config/DefaultEditorPerProjectUserSettings.ini` |
| Sonheim | 18767 | `-ini:` override in its `launch_editor.ps1` |

The plugin default is 18765. One editor owns a port: a second editor on the same port starts without a server and serves no
tools. Check the `project` field of `editor_get_state` before changing anything.

Sonheim loads this plugin from its folder with `-PLUGIN=`, so a build here reaches Sonheim after its editor restarts. Nothing is
copied.

## Building

```
"F:\UE_5.5\Engine\Build\BatchFiles\Build.bat" AgentMcpTestbedEditor Win64 Development -Project="A:\A_Workspace\UE\AgentMcp\AgentMcpTestbed.uproject" -WaitMutex -NoHotReloadFromIDE
```

A running editor holds the DLLs, so the link fails with `LNK1104` until it is closed.

`livecoding_compile` applies a change to a function body in the running editor. It is iteration evidence, not final proof:
a change to `UCLASS`, `USTRUCT`, `UPROPERTY` or `UFUNCTION`, or to module shape, needs the editor closed and a full build before
the result counts.

## Editor automation

- Keep one writer. Serialize MCP writes; run only read-only calls in parallel.
- UE Python files must be UTF-8 without a BOM. A `U+FEFF` at the start makes the call fail with a syntax error.
- A viewport capture or a successful tool call is tooling evidence, not a verdict on how something looks or plays. Say which one
  a result is.
- Do not infer that the editor is reachable from a running process, a configured port or an earlier result file. One read-only
  call that succeeds is the proof.

## Tests

Checks go into the sections of `Tools/mcp_smoke.py`. Do not build a second harness.

```
python Tools/mcp_smoke.py --out Saved/MCP/smoke.json
```

The suite needs the testbed editor on 18766 and takes about ten minutes. `--skip-*` runs one section while iterating.
