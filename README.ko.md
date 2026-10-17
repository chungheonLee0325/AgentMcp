# Agent MCP

**Unreal Engine 5.8 이전 버전을 위한 에디터 MCP + Agent Skills**

[English](README.md) · [문서](Docs/Index.ko.md) · [작업 예제](Docs/Workflows.ko.md)

Agent MCP는 **UE 5.8의 실험적 MCP Toolset을 참고해 에디터 조작 기능을 이전 버전에 구현**하고, **동적 Agent Skill 로딩, UMG 제작·성능 분석 워크플로, 에디터 내 AI 채팅**을 더한 플러그인입니다.

**대상:** UE 5.8 이전 프로젝트에서 Claude Code·Codex 등으로 에디터 작업을 자동화하려는 개발자, UMG 제작과 UI 성능 개선을 반복하는 개발자 및 팀  
**사용 버전:** Unreal Engine 5.5 / 5.7

## 무엇이 다른가?

- **UE 5.8 이전 버전 지원:** 5.8 실험적 MCP Toolset의 기능 구성을 바탕으로 액터·에셋·Blueprint·UMG 편집, 컴파일, PIE 등 에디터 작업 도구를 구현했습니다. Epic 플러그인의 소스 코드를 그대로 이식한 것은 아닙니다.
- **동적 Agent Skills:** 에디터가 `SKILL.md`를 에이전트에 제공하며, 호출할 때 최신 내용을 읽습니다. 기본 Skill을 프로젝트별 규칙으로 재정의할 수도 있습니다.
- **UI 제작과 성능 분석:** UMG 생성 → 컴파일 → 실행 화면 검토와, CSV·Unreal Insights 기반 성능 병목 조사·전후 비교를 위한 도구 및 Skill을 제공합니다.
- **에디터 내 AI 채팅:** Unreal Editor에서 Claude Code, Codex, Gemini(Antigravity)를 연결해 대화로 프로젝트를 조사·수정할 수 있습니다. 권한 모드와 변경 승인 카드를 제공합니다.

## 대표 결과물

Agent MCP 도구와 Skill로 제작한 테스트베드 UI입니다. **UMG Designer에서 위젯을 직접 배치하지 않고** Widget Blueprint 생성·수정, 컴파일, PIE 캡처를 반복했습니다.

| 던전 결과 팝업 | UI 스타일 키트 |
|:---:|:---:|
| ![Agent MCP로 제작한 던전 결과 팝업](Docs/Images/dungeon_result.jpg) | ![Theme Data Asset과 스타일 컴포넌트를 확인하는 UI 키트 갤러리](Docs/Images/ui_kit_gallery.jpg) |

[던전 HUD 보기](Docs/Images/dungeon_hud.jpg) · [UI 제작 사례](Docs/Samples/DungeonUi.ko.md) · [스타일 시스템](Docs/UiStyleSystem.ko.md)

## 주요 기능

| 분야 | 제공 기능 |
|---|---|
| **에디터 제어** | 액터·레벨·에셋 탐색 및 편집, 데이터 에셋·DataTable·스트링 테이블, 에셋 임포트·저장 |
| **Blueprint / UMG** | 그래프·사용처 조회, 노드 편집, Widget Tree와 `BindWidget` 계약 검사, Widget Blueprint 생성·수정 |
| **코드와 실행** | 소스 검색·조회·수정, Blueprint/Live Coding 컴파일, PIE, 로그, 뷰포트 캡처 |
| **Agent Skills** | `skills_list` / `skills_get`, 프로젝트별 Skill 오버라이드, 작업에 필요한 지침과 참고 파일 로딩 |
| **성능 조사** | 반복 CSV 캡처 비교, `CSV_EVENT` 구간 분석, Unreal Insights 타이머·`ProfileGPU` 덤프 분석 |
| **AI 채팅** | Claude Code·Codex·Gemini(Antigravity) CLI, OpenAI/Gemini API 및 OpenAI 호환 엔드포인트, 권한·승인 |

[전체 도구 목록](Docs/Tools.ko.md) · [작업별 호출 순서](Docs/Workflows.ko.md)

## UI 제작 파이프라인

단순히 Widget Blueprint를 생성하는 데 그치지 않고, **컴포넌트 구조와 데이터·스타일 관리, 실제 화면 검토**를 작업 규칙으로 연결합니다.

```text
기존 UI/클래스 조사
  → 재사용 컴포넌트와 C++ BindWidget 계약 설계
  → Widget Blueprint / Widget Tree 생성
  → DataTable · Theme Data Asset · 이미지 연결
  → Blueprint 컴파일
  → PIE 실행 · 화면 캡처 · 로그 확인
  → 수정 반복
```

`umg-authoring`, `ui-style-system`, `ui-art-requests` Skill이 UI 구조·스타일·아트 연결 방식을 안내합니다. 실제 Dungeon UI 제작 과정에서 반복된 위젯 트리를 재사용 컴포넌트로 바꾸고, 스타일을 Theme Data Asset으로 분리했습니다.

[UI 제작 과정 자세히 보기](Docs/Samples/DungeonUi.ko.md)

## UI 성능 분석 파이프라인

`perf-investigation` Skill은 독립 실행 게임에서 **고정 장면을 반복 측정하고 수정 전후의 차이가 실행 간 편차보다 큰지** 확인하도록 안내합니다. 플러그인의 `perf_summarize_csv`, `perf_compare_csv`, `perf_trace_hotspots`, `perf_read_gpu_profile` 도구가 결과 파일을 분석합니다. 반복 실행에는 프로젝트별 성능 장면 실행기(scene runner)가 필요합니다.

```text
기준 장면 반복 측정 → CSV_EVENT로 지연 순간 표시
  → Unreal Insights / GPU 분석 → 원인 수정
  → 동일 조건 재측정 → 전후 비교
```

Sonheim의 인벤토리 UI 개선 사례(1920×1080, 표시한 이벤트의 중앙값):

| 작업 | 수정 전 프레임 (게임 스레드) | 수정 후 프레임 (게임 스레드) |
|---|---:|---:|
| 아이템 두 개 교환 | 32.6ms (19.4ms) | 16.5ms (2.9ms) |
| 인벤토리 열기 | 57.5ms (44.6ms) | 21.7ms (8.1ms) |

원인 분석에서는 슬롯과 툴팁의 반복 재구성, 화면을 열 때마다 발생하는 위젯 생성을 확인했습니다. [성능 측정 워크플로](Docs/Workflows.ko.md#6-성능-측정과-비교)

## 에디터 안에서 AI 채팅

**창 > Agent MCP > AI 채팅** 또는 에디터 툴바의 **AI 채팅**에서 프로젝트에 질문하고 변경을 요청합니다.

- **Claude Code:** Claude 계정으로 CLI 로그인
- **Codex:** ChatGPT 계정으로 CLI 로그인
- **Gemini:** Antigravity CLI를 통해 Google 계정 연결
- **추가 공급자:** OpenAI/Gemini API와 OpenAI 호환 서버

채팅은 **읽기 전용 / 에셋 편집 / 코드 편집 / 에셋+코드 편집** 권한 모드를 제공하고, 변경할 때 승인 카드를 표시합니다. 직접 MCP에 연결한 외부 클라이언트에는 채팅 패널의 승인 카드가 자동 적용되지 않습니다.

[AI 채팅 사용법과 권한](Docs/AiChat.ko.md)

## Agent Skills와 확장 구조

도구는 Unreal Reflection 기반 `static UFUNCTION`으로 정의하고, Skill은 별도 Markdown `SKILL.md`로 제공합니다. 즉 **Tool은 무엇을 실행하는지**, **Skill은 작업을 어떤 구조·순서로 진행하는지**를 담당합니다.

`skills_get`은 Skill 파일을 요청할 때 읽으므로 내용 수정이 에디터 재시작 없이 반영됩니다. 프로젝트의 `AgentMcp/Skills`와 설정된 `SkillDirectories`에서 같은 이름의 기본 Skill을 대체할 수 있습니다. 초기 MCP 안내에 포함된 Skill 목록은 서버 시작 시 생성됩니다.

포함된 Skill: `umg-authoring` · `ui-style-system` · `ui-art-requests` · `blueprint-graphs` · `perf-investigation`

[MCP 아키텍처](Docs/Architecture.ko.md) · [Skill 문서](Docs/Skills.ko.md) · [C++ Toolset 추가 방법](Docs/WritingTools.ko.md)

## 설치 및 연결

1. 이 저장소의 `Plugins/AgentMcp` 폴더를 Unreal 프로젝트의 `Plugins/AgentMcp`에 복사합니다.
2. **Edit > Plugins**에서 **Agent MCP**를 활성화하거나 기존 `.uproject`의 `Plugins` 배열에 `{"Name": "AgentMcp", "Enabled": true}`를 추가합니다.
3. 프로젝트의 **Editor 타깃을 빌드**하고 에디터를 실행합니다.

기본 MCP 주소는 `http://127.0.0.1:18765/mcp`입니다. 에디터는 프로젝트 폴더에 Claude Code용 `.mcp.json`과 Codex용 `.codex/config.toml`을 작성합니다. 인증 토큰, 여러 에디터 실행, 수동 연결 방법은 [클라이언트 연결](Docs/Clients.ko.md)을 참고하세요.

첫 작업은 `editor_get_state`로 대상 프로젝트와 레벨을 확인한 뒤 시작하는 것이 좋습니다.

## 문서와 제약

[문서 목차](Docs/Index.ko.md) · [MCP 아키텍처](Docs/Architecture.ko.md) · [도구 목록](Docs/Tools.ko.md) · [작업 예제](Docs/Workflows.ko.md) · [설정](Docs/Settings.ko.md) · [안전장치](Docs/Safety.ko.md) · [테스트베드](Docs/Testing.ko.md)

- 소스 파일 변경(`source_replace` / `source_write`)은 Unreal Editor Undo로 복구되지 않으므로 소스 컨트롤을 사용해야 합니다.
- Live Coding은 리플렉션 선언(`UCLASS`, `UPROPERTY` 등) 변경을 적용할 수 없습니다.
- 일부 Blueprint·UMG 편집 기능(예: 위젯 애니메이션 생성, 그래프 자동 배치)은 지원하지 않습니다.
- Unreal 5.8의 실험적 MCP Toolset과 Agent Skill 개념을 바탕으로 **이전 버전에 맞게 별도 구현**한 프로젝트입니다. Epic 플러그인 소스나 `UAgentSkill`을 그대로 포팅하지 않았습니다.

[MIT License](LICENSE) · Unreal Engine은 Epic Games, Inc.의 상표입니다.
