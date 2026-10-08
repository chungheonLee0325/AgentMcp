# AI chat

[한국어](AiChat.ko.md)

The `AgentMcpChat` editor module adds a chat panel to the editor, so planners and artists can ask about the project and have it changed without setting up a coding agent. Each person signs in with their own account; the panel uses the same tools as any other MCP client, through the server of the editor it runs in. The panel is in Korean.

Open it from **Window > Agent MCP > AI 채팅** or the **AI 채팅** button on the level editor toolbar.

## Providers

| Provider | Signs in with | Runs |
|---|---|---|
| Claude Code (로그인) | A Claude account | `claude` CLI |
| ChatGPT (Codex 로그인) | A ChatGPT account | `codex` CLI |
| Gemini (Antigravity 로그인) | A Google account, including Google Workspace | `agy` CLI |
| ChatGPT (OpenAI API), Gemini (Google API) | An API key | HTTP from the editor |
| OpenAI 호환 서버 | Optional key | Any server with the OpenAI chat completions API, for example a local Ollama |

**계정** in the panel shows for each provider whether its CLI is installed and signed in, and offers **설치 후 로그인**, **로그인** and **로그아웃** (Claude Code and Codex). The installers are the official PowerShell scripts of each CLI. The model and reasoning pickers in the toolbar list what the signed-in account offers: Codex models come from `codex debug models` and Antigravity models from `agy models`.

## Permission modes

The **권한** picker sets what the model may change. Whatever the mode, every change waits for an approval card in the panel.

| Mode | The model may also use |
|---|---|
| 읽기 전용 (read only) | Read tools only |
| 에셋 편집 (asset edit) | Actor, level, asset, Blueprint, UMG, DataTable, string table, animation and object tools, `editor_undo` and `editor_redo` |
| 코드 편집 (code edit) | `source_write`, `source_replace` and `livecoding_compile` |
| 에셋 + 코드 편집 | Both |

In every mode the model may open and show things: `editor_open_assets`, `editor_show_in_content_browser`, `actor_select` and `viewport_set_camera` run without a card. `level_open` is allowed in every mode but asks first, because it replaces the open level. `tools_call` is never offered.

The mode is enforced twice: the CLI is offered only the allowed tools, and the editor's approval gate refuses any other tool a CLI calls (see [Safety](Safety.md)). An approval card offers **승인**, **거부** and **이번 질문 동안 모두 승인** (approve everything until the answer ends).

## What the CLIs can reach

Each CLI runs in a folder of its own under `Saved/AgentMcpChat/<CLI>`, with no built-in tools: no shell, no file access and no web. It reaches the project only through the editor's MCP tools, and reads code through the source tools.

- **Claude Code** runs with `--strict-mcp-config`, `--restricted` (the user's own settings files and hooks are ignored), `--permission-prompts none` and `--tools ""`.
- **Codex** runs with `--ignore-user-config` and `--ignore-rules`, a read-only sandbox, web search off, and its shell, browser, app, plugin and sub-agent features disabled. The user's `~/.codex/config.toml` is not read or changed; the login in `~/.codex` is used.
- **Antigravity** gets the server in `.agents/mcp_config.json` of its folder. Because `agy -p` cannot ask for permission, the panel adds the rule `mcp(agentmcp-chat/*)` to `~/.gemini/antigravity-cli/settings.json` once; the editor's approval gate still decides every change. When the CLI ends a turn after a refused call, the panel retries once.

## Conversations

**대화 목록** lists earlier conversations and **새 대화** starts one. Conversations are stored per project in `Saved/AgentMcpChat/Conversations/<id>.json` and continue the CLI session they started in. The panel answers one question at a time.

## Settings

Per-user settings live in the user's `EditorSettings.ini`, not in the project, so API keys are never committed. They are in **Editor Preferences > Plugins > AI 채팅**:

| Setting | Meaning |
|---|---|
| 공급자 (Provider), 권한 모드 (Mode) | Also picked in the panel |
| 실행 파일 | Paths of `claude.exe`, `agy.exe` and `codex.exe`; empty finds the installer's location and then `PATH` |
| API 키 | OpenAI and Gemini keys; empty uses `OPENAI_API_KEY` or `GEMINI_API_KEY` |
| OpenAI 호환 서버 | Base URL, key and model of the custom server |
| 제한 | Tool rounds and request timeout of the API providers |

The chosen models and reasoning levels are stored with these settings but are picked only in the panel.

The console command `AgentMcpChat.Ask <question>` sends a question with the current settings and writes the conversation to the log (`LogAgentMcpChat`), which helps check a provider without the panel. `AgentMcpChat.Ask reset` starts over.

## Limitations

- Windows only: the install and sign-in commands are the Windows ones.
- Gemini models through Antigravity take the reasoning levels low, medium and high only.
- The panel has been used with Claude Code, Codex and Antigravity on Unreal Engine 5.7.4. The API providers have had less use.
