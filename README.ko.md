# Agent MCP — Unreal Editor용 MCP 서버

[English](README.md) · [문서 목차](Docs/Index.ko.md) · [작업 예제](Docs/Workflows.ko.md)

Agent MCP는 **Claude Code, Codex 같은 AI 코딩 에이전트가 Unreal Editor의 프로젝트를 직접 조사하고 수정할 수 있도록 연결하는 플러그인**입니다. 에셋·Blueprint·UMG·레벨을 읽고 편집하며, 컴파일·Play In Editor(PIE)·로그·뷰포트 캡처로 변경 결과를 확인할 수 있습니다.

```text
프로젝트 조사 → 변경 → 컴파일 → PIE 실행 → 로그/화면 확인 → 수정
```

**상태: Beta.** Windows 64비트의 Unreal Engine **5.5.4**에서 빌드·테스트했습니다. UE **5.7.4**에서도 빌드해 사용 중이지만 전체 smoke 테스트는 아직 실행하지 않았습니다. 다른 플랫폼은 확인하지 않았습니다.

## 주요 기능

| 작업 | Agent MCP가 제공하는 것 |
|---|---|
| **프로젝트와 에셋 조사** | 액터·에셋·클래스 탐색, 프로퍼티 조회, 의존성/참조 검색, 콘텐츠 브라우저에서 에셋 선택 |
| **UMG 제작과 수정** | Widget Tree·Named Slot 검사, C++ `BindWidget` 계약 확인, Widget Blueprint 생성 및 트리·슬롯 편집 |
| **Blueprint 로직** | 그래프 읽기, 노드 타입 검색·연결·편집, 함수·변수·이벤트 디스패처 추가, `blueprint_find_usages` 참조 검색 |
| **소스 코드 작업** | `source_find` / `source_search` / `source_read`, 범위가 제한된 파일 편집, CP949 등 인코딩 유지 |
| **콘텐츠 제작** | 레벨·액터 배치, DataTable·데이터 에셋·스트링 테이블, 텍스처·메시 임포트, 일부 애니메이션 에셋 제작 |
| **실행과 확인** | Blueprint 컴파일, Live Coding, PIE 실행, 에디터 로그 조회, 게임 UI를 포함한 뷰포트 캡처 |
| **성능 분석** | CSV 반복 측정 요약·비교, Unreal Insights 타이머, GPU 프로파일 덤프, 측정 환경 점검 |

[전체 도구 및 접근 수준](Docs/Tools.ko.md) · [작업별 사용 순서](Docs/Workflows.ko.md)

## 빠른 시작

### 1. 프로젝트에 플러그인 설치

1. 이 저장소의 `Plugins/AgentMcp` 폴더를 Unreal 프로젝트의 `Plugins/AgentMcp` 경로에 복사합니다.
2. Unreal Editor의 **Edit > Plugins**에서 **Agent MCP**를 활성화합니다. 또는 기존 `.uproject`의 `Plugins` 배열에 `{"Name": "AgentMcp", "Enabled": true}` 항목을 추가합니다.
3. 플러그인은 소스로 배포되므로 프로젝트의 **Editor 타깃을 빌드**하고 에디터를 실행합니다.

### 2. MCP 서버 연결 확인

에디터 Output Log에서 다음 형식의 로그를 확인합니다.

```text
LogAgentMcpProtocol: Agent MCP server listening on http://127.0.0.1:18765/mcp
```

기본 포트는 `18765`입니다. 이 저장소의 테스트베드 프로젝트는 `18766`을 사용합니다. 서버는 에디터 내부에서 실행되므로 대상 에디터가 열려 있어야 합니다.

에디터가 시작할 때 프로젝트 루트에 Claude Code용 `.mcp.json`과 Codex용 `.codex/config.toml`을 작성합니다. Codex는 **신뢰한 프로젝트**에서만 해당 설정을 읽습니다. 별도 인증 토큰을 사용하거나, 여러 에디터를 실행하거나, 자동 생성 대신 수동으로 연결하려면 [클라이언트 연결](Docs/Clients.ko.md)을 참고하세요.

### 3. 첫 도구 호출

연결된 에이전트에게 먼저 다음을 요청하세요.

> `editor_get_state`로 현재 Unreal 프로젝트와 열린 레벨, PIE 상태, 저장되지 않은 변경을 확인해 줘. 수정 작업은 하지 마.

이후 에셋을 찾으려면 `asset_find`, Blueprint 구조를 조사하려면 `blueprint_inspect`, UMG를 조사하려면 `umg_inspect`를 사용합니다. 첫 변경 전에 대상 프로젝트와 작업 범위를 확인하는 것을 권장합니다.

## 실제 작업 예제

**Blueprint 함수 변경 전 사용처 조사**

```text
editor_get_state
  → blueprint_inspect
  → blueprint_find_usages
  → blueprint_get_graph / blueprint_read_graph
  → 영향 범위 확인 후 수정
```

**UMG 화면 제작과 검토**

```text
umg_create_widget_blueprint
  → umg_add_widgets
  → blueprint_compile
  → pie_start → viewport_capture → log_get_recent
  → pie_stop → asset_save
```

도구별 인자와 주의사항은 [작업 예제](Docs/Workflows.ko.md)에 정리했습니다.

## Dungeon UI — 도구와 제작 규칙을 함께 개선한 사례

테스트베드의 `Content/Samples/DungeonUi`에는 Claude Code가 Agent MCP 도구로 만든 던전 HUD와 결과 팝업이 있습니다. UMG Designer에서 화면을 직접 조작하지 않고 Widget Blueprint를 구성한 사례입니다.

![Dungeon result popup](Docs/Images/dungeon_result.jpg)

![Dungeon progress HUD](Docs/Images/dungeon_hud.jpg)

초기 버전은 반복 요소가 복사되고 스타일 값이 위젯에 분산되어 있었습니다. 이후 **재사용 Widget Blueprint 컴포넌트, C++ `BindWidget` 계약, `DynamicEntryBox`, Theme Data Asset, DataTable**을 적용했습니다. 그 과정에서 Tool이 반환하는 불필요한 프로퍼티 값을 줄이고, 재사용과 화면 검토에 관한 제작 규칙은 `umg-authoring` Skill로 분리했습니다.

[Dungeon UI 제작 과정](Docs/Samples/DungeonUi.ko.md) · [UI 스타일 시스템](Docs/UiStyleSystem.ko.md)

## 성능 분석 — 측정에서 전후 비교까지

`perf-investigation` Skill은 **고정 장면의 독립 실행 게임을 반복 측정**하고, Toolset은 결과 파일을 분석합니다. `perf_summarize_csv`와 `perf_compare_csv`는 CSV 프로파일러의 프레임·이벤트 값을 요약하고 실행 간 편차를 함께 비교합니다. `perf_trace_hotspots`는 Unreal Insights 트레이스의 타이머 비용을 분석하며 `perf_read_gpu_profile`은 `ProfileGPU` 덤프를 읽습니다.

```text
장면 설정 → 기준 반복 측정 → CSV_EVENT로 순간 표시 → 트레이스 분석
      → 원인 수정 → 동일 조건 재측정 → 편차를 고려한 비교
```

Sonheim에서 사용한 예시 (1920×1080, 이벤트 중앙값; 프레임 시간과 괄호 안 게임 스레드 시간):

| 순간 | 수정 전 | 수정 후 | 분석에서 확인한 원인 |
|---|---|---|---|
| 인벤토리 아이템 두 개 교환 | 32.6ms (19.4ms) | 16.5ms (2.9ms) | 모든 슬롯 재구성 및 툴팁 재연결 |
| 인벤토리 열기 | 57.5ms (44.6ms) | 21.7ms (8.1ms) | 화면·42개 슬롯·아이템별 툴팁 재생성 |

단일 실행의 차이가 아니라 같은 조건의 반복 실행과 편차를 확인하도록 설계했습니다. [성능 측정 작업 순서](Docs/Workflows.ko.md#6-성능-측정과-비교)

## 도구와 작업 규칙의 분리

Agent MCP 도구는 `UAgentMcpToolset` 하위 클래스의 `static UFUNCTION`으로 작성합니다. Unreal Reflection에서 도구 이름, 인자와 반환 JSON 스키마를 생성합니다. 새 도구를 C++에 추가하는 방법은 [도구 만들기](Docs/WritingTools.ko.md)를 참고하세요.

**Tool은 실행 가능한 편집·조회 기능**, **Skill은 그 기능을 어떤 순서와 기준으로 사용할지에 관한 작업 지침**입니다. `SKILL.md`는 플러그인에서 제공하고 프로젝트별 지침으로 덮어쓸 수 있습니다.

포함된 Skill:
- `umg-authoring`: 컴포넌트 재사용, `BindWidget`, 데이터 기반 목록과 검토
- `ui-style-system`: 디자인 토큰, 스타일 추출·적용과 UI 갤러리
- `ui-art-requests`: 이미지 요청, 검수, 임포트와 UI 연결
- `blueprint-graphs`: Blueprint 클래스·그래프 제작과 실행 확인
- `perf-investigation`: 장면별 반복 측정, 원인 추적, 편차를 고려한 전후 비교

[Skill 구조와 읽는 방법](Docs/Skills.ko.md)

## 에디터 내 AI 채팅

별도의 Claude Code/Codex MCP 클라이언트를 열지 않고 에디터의 **창 > Agent MCP > AI 채팅**에서 프로젝트에 질문하거나 작업을 요청할 수도 있습니다. CLI 로그인 또는 API 공급자를 설정할 수 있으며 권한 모드와 변경 승인 카드를 제공합니다.

**주의:** 채팅 패널의 승인 카드는 직접 MCP로 연결한 외부 클라이언트에 자동 적용되지 않습니다. 연결·승인 방식의 차이는 [AI 채팅](Docs/AiChat.ko.md)과 [안전장치](Docs/Safety.ko.md)를 확인하세요.

## 문서

[문서 목차](Docs/Index.ko.md)에서 목적별로 찾거나 아래에서 바로 이동할 수 있습니다.

- [클라이언트 연결](Docs/Clients.ko.md) · [설정](Docs/Settings.ko.md) · [안전장치](Docs/Safety.ko.md)
- [전체 도구](Docs/Tools.ko.md) · [작업 예제](Docs/Workflows.ko.md) · [Agent Skills](Docs/Skills.ko.md)
- [AI 채팅](Docs/AiChat.ko.md) · [Toolset 확장](Docs/WritingTools.ko.md) · [테스트베드와 smoke 테스트](Docs/Testing.ko.md)

## 현재 제한 사항

- 요청은 에디터 게임 스레드에서 처리됩니다. 백그라운드에서 **Use Less CPU when in Background**가 활성화되면 호출이 느려질 수 있습니다.
- 결과는 일반 JSON이며 SSE나 진행 상황 스트리밍은 제공하지 않습니다.
- `source_replace` / `source_write`는 에디터 Undo로 복구되지 않습니다. 소스 변경에는 Git 등 버전 관리를 사용하세요.
- Live Coding은 `UCLASS`, `USTRUCT`, `UPROPERTY`, `UFUNCTION` 선언 변경을 적용하지 못합니다.
- Blueprint 멤버 편집, 그래프 자동 배치, UMG 애니메이션 편집 등 일부 기능은 지원 범위가 제한됩니다.
- Claude Code에서 주요 도구를 사용해 샘플을 제작했습니다. Codex는 AI 채팅 패널에서 사용했으며 `.codex/config.toml` 직접 연결 경로는 별도로 검증하지 않았습니다.

상세한 범위는 [도구](Docs/Tools.ko.md), [안전장치](Docs/Safety.ko.md), [테스트](Docs/Testing.ko.md)를 참고하세요.

## 구현 배경과 라이선스

Unreal Engine 5.8의 실험적 MCP Toolset과 Agent Skill 개념을 참고해 **UE 5.5용으로 별도 구현**했습니다. Epic의 플러그인 소스나 `UAgentSkill`을 포팅한 프로젝트는 아닙니다. Unreal Engine은 Epic Games, Inc.의 상표입니다.

[MIT License](LICENSE)
