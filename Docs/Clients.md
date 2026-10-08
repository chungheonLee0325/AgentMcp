# Connecting a client

[한국어](Clients.ko.md)

The server speaks MCP Streamable HTTP with JSON responses at `http://127.0.0.1:18765/mcp`.

The editor writes both files on startup, so this section is what they contain and how to write them by hand.

For Claude Code, add a `.mcp.json` file to the project root:

```json
{
  "mcpServers": {
    "unreal": { "type": "http", "url": "http://127.0.0.1:18765/mcp" }
  }
}
```

If `AuthToken` is set, add `"headers": { "Authorization": "Bearer <token>" }` to the server entry.

For Codex, add a `.codex/config.toml` file to the project root. Codex reads it only in a trusted project, one that `~/.codex/config.toml` lists with `trust_level = "trusted"` after you trust the folder.

```toml
[mcp_servers.unreal]
url = "http://127.0.0.1:18765/mcp"
tool_timeout_sec = 600
```

`tool_timeout_sec` raises the Codex default of 60 seconds, which compiling, saving and play sessions can exceed. If `AuthToken` is set, add `http_headers = { Authorization = "Bearer <token>" }`, or name an environment variable that holds the token with `bearer_token_env_var`. Start a new Codex session after changing the file.

The server returns short usage instructions from `initialize`, including the list of skills. Agents should start with `editor_get_state`.

**One port per editor.** Every editor that enables the plugin uses the configured port. When two editors run at the same time, the second one cannot bind the port, logs `Agent MCP server failed to start`, and serves no tools. Give projects that run at the same time different ports, and check the `project` field of `editor_get_state` before changing anything.
