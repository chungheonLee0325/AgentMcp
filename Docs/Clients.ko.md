# 클라이언트 연결

[English](Clients.md)

서버는 `http://127.0.0.1:18765/mcp`에서 MCP Streamable HTTP(JSON 응답)로 통신합니다.

Claude Code에서는 프로젝트 루트에 `.mcp.json` 파일을 추가합니다.

```json
{
  "mcpServers": {
    "unreal": { "type": "http", "url": "http://127.0.0.1:18765/mcp" }
  }
}
```

`AuthToken`을 설정했다면 서버 항목에 `"headers": { "Authorization": "Bearer <token>" }`를 추가합니다.

Codex에서는 프로젝트 루트에 `.codex/config.toml` 파일을 추가합니다. Codex는 신뢰한 프로젝트에서만 이 파일을 읽습니다. 폴더를 신뢰하면 `~/.codex/config.toml`에 그 폴더가 `trust_level = "trusted"`로 기록됩니다.

```toml
[mcp_servers.unreal]
url = "http://127.0.0.1:18765/mcp"
tool_timeout_sec = 600
```

`tool_timeout_sec`는 Codex의 기본 제한시간 60초를 늘립니다. 컴파일, 저장, 플레이 세션은 60초를 넘길 수 있습니다. `AuthToken`을 설정했다면 `http_headers = { Authorization = "Bearer <token>" }`를 추가하거나, 토큰이 든 환경 변수 이름을 `bearer_token_env_var`에 적습니다. 파일을 바꾼 뒤에는 Codex 세션을 새로 시작합니다.

서버는 `initialize` 응답으로 짧은 사용 안내와 스킬 목록을 보냅니다. 에이전트는 `editor_get_state`로 작업을 시작하는 것이 좋습니다.

**에디터마다 포트 하나.** plugin을 켠 에디터는 모두 설정된 포트를 씁니다. 에디터 두 개를 동시에 실행하면 두 번째 에디터는 포트를 열지 못해 `Agent MCP server failed to start`를 기록하고 도구를 제공하지 않습니다. 동시에 여는 프로젝트에는 서로 다른 포트를 지정하고, 무언가를 바꾸기 전에 `editor_get_state`의 `project` 값을 확인하세요.
