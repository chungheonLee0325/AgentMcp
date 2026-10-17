# Agent MCP 문서

[English](Index.md) · [README](../README.ko.md)

Agent MCP는 Unreal Editor 안에서 MCP 서버를 실행해 에이전트가 프로젝트를 조사하고, 수정하고, 빌드·플레이 결과를 확인하도록 연결합니다. 이 문서는 **어떤 작업을 하려는가**를 기준으로 관련 설명을 찾는 출발점입니다.

## 처음 사용하는 경우

| 목적 | 문서 |
|---|---|
| 플러그인 설치와 첫 연결 | [README 빠른 시작](../README.ko.md#빠른-시작) |
| Claude Code / Codex 연결, 인증과 다중 에디터 | [클라이언트 연결](Clients.ko.md) |
| 실제 작업별 도구 호출 순서 | [작업 예제](Workflows.ko.md) |
| 도구 이름과 접근 수준 확인 | [전체 도구 목록](Tools.ko.md) |
| 프로젝트 설정, 서버 포트 및 접근 제한 | [설정](Settings.ko.md) |
| 편집 시 제한과 승인 범위 | [안전장치](Safety.ko.md) |

## 기능별 설명

| 분야 | 읽을 문서 | 다루는 내용 |
|---|---|---|
| Widget Blueprint와 UI | [UMG 샘플](Samples/DungeonUi.ko.md) | `BindWidget`, 재사용 컴포넌트, 데이터 기반 목록, 제작·수정·캡처 |
| UI 스타일 | [UI 스타일 시스템](UiStyleSystem.ko.md) | Theme Data Asset, 스타일 위젯, 스타일 검토 |
| Blueprint / 레벨 / 에셋 | [도구](Tools.ko.md) | 그래프 조회·편집, 참조 검색, 데이터와 액터 작업 |
| 에이전트 작업 규칙 | [Skills](Skills.ko.md) | 플러그인·프로젝트별 `SKILL.md`, 오버라이드 |
| 에디터 내 채팅 | [AI 채팅](AiChat.ko.md) | 모델 공급자, 로그인, 권한 모드, 변경 승인 |
| Toolset 확장 | [도구 만들기](WritingTools.ko.md) | `UFUNCTION`과 Reflection 기반 MCP 도구 추가 |
| 성능 조사 | [성능 워크플로](Workflows.ko.md#6-성능-측정과-비교) | CSV 프로파일링, Insights, GPU 덤프, 전후 비교 |
| 검증 환경 | [테스트베드](Testing.ko.md) | Smoke 테스트와 샘플 프로젝트 실행 |

## 동작 구조

```text
Claude Code / Codex             에디터 내 AI 채팅
         \                         /
          \                       /
           MCP 요청 (127.0.0.1)
                    |
             AgentMcpProtocol
                    |
      Reflection 기반 Toolset 등록
                    |
          AgentMcpTools / Toolset
                    |
           Unreal Editor API
                    |
     Assets / UMG / Blueprint / PIE
```

도구의 입력·반환 스키마는 C++ `UFUNCTION` 선언과 Unreal Reflection을 기반으로 생성됩니다. 작업 방법에 관한 지침은 별도 `SKILL.md`로 제공하므로 **도구의 기능**과 **에이전트가 따라야 할 제작 규칙**을 구분합니다.

## 작업 전에 알아둘 점

- 읽기, 편집, 삭제, 에디터 상태 변경의 접근 수준은 서로 다릅니다. 구분은 [도구](Tools.ko.md)와 [안전장치](Safety.ko.md)에 설명되어 있습니다.
- **에디터 내 AI 채팅의 승인 카드와 직접 연결한 Claude Code / Codex는 동일한 승인 흐름이 아닙니다.** 승인 게이트는 클라이언트 태그로 구분합니다.
- 에셋 편집의 Undo와 **소스 파일 수정의 복구 방법은 다릅니다.** `source_replace` / `source_write`는 `editor_undo`로 되돌릴 수 없으므로 Git 등 버전 관리를 사용하세요.
- 작업 대상 프로젝트가 맞는지 `editor_get_state`에서 확인한 후 수정하는 것이 좋습니다.

현재 문서는 저장소의 `Docs/` 폴더에서 관리됩니다. 별도의 GitHub Wiki와 혼동하지 않도록, README와 이 목차에서 실제 문서 파일로 직접 연결합니다.
