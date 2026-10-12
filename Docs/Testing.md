# Testbed and smoke test

[한국어](Testing.ko.md)

The repository root is a small Unreal Engine 5.5 project that builds and tests the plugin.

| Path | Contents |
|---|---|
| `Plugins/AgentMcp` | The plugin, with its skills in `Plugins/AgentMcp/Skills` |
| `Source/AgentMcpTestbed` | Row structs, data assets, widget bases, game mode and the C++ classes of the UI sample |
| `Source/AgentMcpTestbedEditor` | Test-only tools and `sample_show_widget` |
| `Content/Samples/DungeonUi` | Widget Blueprints, theme data asset and item table built through the tools |
| `Content/Samples/Pickup` | A spinning, bobbing pickup Blueprint (variables, a pure function, construction script and event graph) and its demo level, built through the blueprint tools |
| `Art/Requests` | Art requests for the sample |
| `Docs` | Case studies, the UI style system of the sample, and experiments |
| `.mcp.json`, `.codex/config.toml` | Claude Code and Codex connection settings for testbed port 18766 |
| `.claude/skills`, `.agents/skills` | Bridges that let clients begin the served plugin skills |
| `Config` | Testbed port **18766**, the smoke test's skill folder in `SkillDirectories`, and the UI sample's theme in `DefaultGame.ini` |
| `Tools/mcp_smoke.py` | Smoke test using Python 3 standard library only |
| `Tools/mcp_call.py` | Calls one tool from the command line |

1. Build the `AgentMcpTestbedEditor` target.  
   `<UE>\Engine\Build\BatchFiles\Build.bat AgentMcpTestbedEditor Win64 Development -Project=<path>\AgentMcpTestbed.uproject -WaitMutex`
2. Open `AgentMcpTestbed.uproject` and wait for `Agent MCP server listening on http://127.0.0.1:18766/mcp`.
3. Run `python Tools/mcp_smoke.py --out Saved/MCP/smoke.json`.

The smoke test covers MCP transport and errors, every tool, undo and rollback, request cancellation, Play In Editor, viewport capture with game UI, Live Coding, nested Widget Blueprint editing, Blueprint graph editing, skills, data-asset/DataTable creation, texture and mesh import, level authoring and animation assets.

P9 checks the source tools (ranges, regular expressions, CP949 and CRLF, refusals), the navigation tools and the approval gate for tagged clients. It needs no testbed content, so it also runs against another project; skip the sections that need the testbed:

```bash
python Tools/mcp_smoke.py --url http://127.0.0.1:<port>/mcp --expect-project <Project> --skip-slice1 --skip-p1 --skip-p2 --skip-p3 --skip-p4 --skip-p5 --skip-p6 --skip-p7 --skip-p8
```

P9 writes two text files at the top of the first readable folder (`Config` by default) and removes them again.

To call a single tool:

```bash
python Tools/mcp_call.py editor_get_state --url http://127.0.0.1:18766/mcp --expect-project AgentMcpTestbed
```
