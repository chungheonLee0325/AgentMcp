# AI 채팅

[English](AiChat.md)

`AgentMcpChat` 에디터 모듈은 에디터 안에 채팅 패널을 추가합니다. 기획자와 아티스트도 코딩 에이전트를 따로 설정하지 않고 프로젝트에 대해 묻고 변경을 맡길 수 있습니다. 각자 자기 계정으로 로그인하며, 패널은 실행 중인 에디터의 서버를 통해 다른 MCP 클라이언트와 같은 도구를 씁니다. 패널은 한국어로 되어 있습니다.

**창 > Agent MCP > AI 채팅** 또는 레벨 에디터 툴바의 **AI 채팅** 버튼으로 엽니다.

## 공급자

| 공급자 | 로그인 | 실행 |
|---|---|---|
| Claude Code (로그인) | Claude 계정 | `claude` CLI |
| ChatGPT (Codex 로그인) | ChatGPT 계정 | `codex` CLI |
| Gemini (Antigravity 로그인) | Google 계정(Google Workspace 포함) | `agy` CLI |
| ChatGPT (OpenAI API), Gemini (Google API) | API 키 | 에디터에서 HTTP |
| OpenAI 호환 서버 | 키 선택 | OpenAI chat completions API를 쓰는 서버(예: 로컬 Ollama) |

패널의 **계정**은 공급자마다 CLI가 설치되어 있는지, 로그인되어 있는지 보여 주고 **설치 후 로그인**, **로그인**, **로그아웃**(Claude Code, Codex)을 제공합니다. 설치는 각 CLI의 공식 PowerShell 스크립트로 합니다. 툴바의 모델과 추론 선택 목록은 로그인한 계정이 쓸 수 있는 것을 보여 줍니다. Codex 모델은 `codex debug models`, Antigravity 모델은 `agy models`에서 가져옵니다.

## 권한 모드

툴바의 **권한**은 모델이 무엇을 바꿀 수 있는지 정합니다. 어떤 모드든 변경은 패널의 승인 카드를 기다립니다.

| 모드 | 추가로 쓸 수 있는 도구 |
|---|---|
| 읽기 전용 | Read 도구만 |
| 에셋 편집 | 액터, 레벨, 에셋, 블루프린트, UMG, DataTable, 스트링 테이블, 애니메이션, 오브젝트 도구와 `editor_undo`, `editor_redo` |
| 코드 편집 | `source_write`, `source_replace`, `livecoding_compile` |
| 에셋 + 코드 편집 | 둘 다 |

어떤 모드에서든 열고 보여 주는 것은 할 수 있습니다. `editor_open_assets`, `editor_show_in_content_browser`, `actor_select`, `viewport_set_camera`는 카드 없이 실행합니다. `level_open`은 열린 레벨을 바꾸므로 모든 모드에서 허용하되 먼저 묻습니다. `tools_call`은 제공하지 않습니다.

모드는 두 번 적용됩니다. CLI에는 허용된 도구만 제공하고, CLI가 다른 도구를 부르면 에디터의 승인 게이트가 거부합니다([안전장치](Safety.ko.md) 참고). 승인 카드에는 **승인**, **거부**, **이번 질문 동안 모두 승인**이 있습니다.

## CLI가 접근할 수 있는 범위

CLI는 각자 `Saved/AgentMcpChat/<CLI>` 폴더에서 내장 도구 없이 실행됩니다. 셸, 파일 접근, 웹이 없고, 프로젝트에는 에디터의 MCP 도구로만 접근하며 코드는 소스 도구로 읽습니다.

- **Claude Code**는 `--strict-mcp-config`, `--restricted`(사용자 설정 파일과 hook 무시), `--permission-prompts none`, `--tools ""`로 실행합니다.
- **Codex**는 `--ignore-user-config`, `--ignore-rules`, 읽기 전용 sandbox, 웹 검색 끔으로 실행하고 셸, 브라우저, 앱, plugin, 하위 에이전트 기능을 끕니다. 사용자의 `~/.codex/config.toml`은 읽지도 바꾸지도 않고, `~/.codex`의 로그인만 씁니다.
- **Antigravity**는 자기 폴더의 `.agents/mcp_config.json`으로 서버를 받습니다. `agy -p`는 권한을 물을 수 없어서, 패널이 `~/.gemini/antigravity-cli/settings.json`에 `mcp(agentmcp-chat/*)` 규칙을 한 번 추가합니다. 변경 여부는 여전히 에디터의 승인 게이트가 정합니다. 거부된 호출 뒤 CLI가 턴을 끝내면 패널이 한 번 다시 시도합니다.

## 대화

**대화 목록**은 이전 대화를 보여 주고 **새 대화**는 새로 시작합니다. 대화는 프로젝트별로 `Saved/AgentMcpChat/Conversations/<id>.json`에 저장되고, 시작한 CLI 세션을 이어 갑니다. 패널은 한 번에 질문 하나에 답합니다.

## 설정

사용자별 설정은 프로젝트가 아니라 사용자의 `EditorSettings.ini`에 저장되므로 API 키가 커밋되지 않습니다. **에디터 환경설정 > 플러그인 > AI 채팅**에 있습니다.

| 설정 | 의미 |
|---|---|
| 공급자, 권한 모드 | 패널에서도 고름 |
| 실행 파일 | `claude.exe`, `agy.exe`, `codex.exe` 경로. 비워 두면 설치 위치와 `PATH`에서 찾음 |
| API 키 | OpenAI와 Gemini 키. 비워 두면 `OPENAI_API_KEY`, `GEMINI_API_KEY` 사용 |
| OpenAI 호환 서버 | 사용자 서버의 기본 URL, 키, 모델 |
| 제한 | API 공급자의 도구 라운드와 요청 제한 시간 |

고른 모델과 추론 수준도 이 설정에 저장되지만 고르는 곳은 패널뿐입니다.

콘솔 명령 `AgentMcpChat.Ask <질문>`은 현재 설정으로 질문을 보내고 대화를 로그(`LogAgentMcpChat`)에 남깁니다. 패널 없이 공급자를 확인할 때 씁니다. `AgentMcpChat.Ask reset`은 처음부터 다시 시작합니다.

## 한계

- Windows 전용입니다. 설치와 로그인 명령이 Windows 기준입니다.
- Antigravity를 통한 Gemini 모델의 추론 수준은 낮음, 보통, 높음만 됩니다.
- 패널은 Unreal Engine 5.7.4에서 Claude Code, Codex, Antigravity로 써 봤습니다. API 공급자는 덜 써 봤습니다.
