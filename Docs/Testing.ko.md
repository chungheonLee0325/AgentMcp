# 테스트베드와 smoke 테스트

[English](Testing.md)

저장소 루트는 plugin을 빌드하고 테스트하는 작은 Unreal Engine 5.5 프로젝트입니다.

| 경로 | 내용 |
|---|---|
| `Plugins/AgentMcp` | plugin. 스킬은 `Plugins/AgentMcp/Skills`에 있음 |
| `Source/AgentMcpTestbed` | 테스트용 행 구조체, 데이터 에셋 클래스, 위젯 부모 클래스, 게임 모드, UI 샘플의 C++ 클래스 |
| `Source/AgentMcpTestbedEditor` | `/Game/AgentMcpFixtures` 아래에 테스트 에셋을 만드는 `testbed_*` 도구, 롤백·취소 검사용 훅, `sample_show_widget` |
| `Content/Samples/DungeonUi` | 도구로 만든 UI 샘플의 위젯 블루프린트, 테마 데이터 에셋, 아이템 테이블 |
| `Art/Requests` | 샘플의 아트 요청 |
| `Docs` | 샘플을 만든 과정과 계획한 실험 |
| `.mcp.json`, `.codex/config.toml` | Claude Code와 Codex를 포트 18766의 테스트베드 에디터에 연결 |
| `.claude/skills`, `.agents/skills` | Claude Code와 Codex가 플러그인 스킬을 시작하게 하는 짧은 스킬 파일 |
| `Config` | 테스트베드는 포트 **18766**을 사용하며 UI 샘플의 테마를 지정 |
| `Tools/mcp_smoke.py` | smoke 테스트(Python 3, 표준 라이브러리만 사용) |
| `Tools/mcp_call.py` | 명령줄에서 도구 하나 호출 |

1. `AgentMcpTestbedEditor` 타깃을 빌드합니다.  
   `<UE>\Engine\Build\BatchFiles\Build.bat AgentMcpTestbedEditor Win64 Development -Project=<path>\AgentMcpTestbed.uproject -WaitMutex`
2. `AgentMcpTestbed.uproject`를 열고 `Agent MCP server listening on http://127.0.0.1:18766/mcp`가 나올 때까지 기다립니다.
3. `python Tools/mcp_smoke.py --out Saved/MCP/smoke.json`을 실행합니다.

smoke 테스트는 MCP 전송과 오류 처리, 모든 도구, undo와 롤백, 요청 취소, Play In Editor, 게임 UI를 포함한 뷰포트 캡처, Live Coding, 중첩된 위젯 블루프린트 인스턴스를 포함한 편집, 스킬, 데이터 에셋·DataTable 생성, 텍스처와 메시 가져오기, 레벨 제작, 애니메이션 에셋을 검사합니다.

P9는 소스 도구(줄 범위, 정규식, CP949와 CRLF, 거부), 탐색 도구, 태그가 붙은 클라이언트의 승인 게이트를 검사합니다. 테스트베드 콘텐츠가 필요 없어서 다른 프로젝트에도 돌릴 수 있습니다. 테스트베드가 필요한 섹션은 건너뛰세요.

```bash
python Tools/mcp_smoke.py --url http://127.0.0.1:<port>/mcp --expect-project <Project> --skip-slice1 --skip-p1 --skip-p2 --skip-p3 --skip-p4 --skip-p5 --skip-p6 --skip-p7 --skip-p8
```

P9는 읽을 수 있는 첫 폴더(기본값 `Config`) 맨 위에 텍스트 파일 두 개를 썼다가 지웁니다.

도구 하나만 호출하려면:

```bash
python Tools/mcp_call.py editor_get_state --url http://127.0.0.1:18766/mcp --expect-project AgentMcpTestbed
```
