# Agent Skills — 프로젝트별 작업 지침을 MCP로 제공

[English](Skills.md) · [전체 도구](Tools.ko.md) · [작업 예제](Workflows.ko.md)

**Tool은 Unreal Editor에서 실행할 수 있는 기능**, **Skill은 그 기능을 어떤 순서와 기준으로 사용할지 정한 작업 지침**입니다. AgentMcp는 지침을 Claude Code 한 곳에 고정하지 않고 에디터의 MCP 서버에서 제공합니다. UE 5.8의 Agent Skill 개념을 참고하되, 이전 버전에서도 수정·공유하기 쉽도록 Markdown 기반으로 별도 구현했습니다.

## Tool과 Skill을 분리한 이유

던전 UI 샘플을 처음 만들 때 에이전트는 Widget Blueprint를 생성할 수 있었지만, 반복되는 UI 요소를 복사하고 스타일 값을 각 위젯에 직접 넣었습니다. **위젯을 생성하는 기능이 있다고 해서 유지보수 가능한 화면 구조까지 결정되는 것은 아니었습니다.**

그래서 UMG 도구에는 *Widget Tree를 읽고 수정하는 방법*을 두고, `umg-authoring` Skill에는 *재사용 컴포넌트, C++ `BindWidget` 계약, 데이터 기반 목록, 실행 화면 검토*에 관한 작업 규칙을 넣었습니다.

| 구분 | 역할 | 예시 |
|---|---|---|
| MCP Tool | 실제 프로젝트 상태를 조회·변경 | `umg_inspect`, `umg_add_widgets`, `blueprint_compile`, `viewport_capture` |
| Agent Skill | 조사·설계·수정·확인 순서를 안내 | UI를 만들기 전에 기존 컴포넌트를 찾고, 반복 목록은 재사용 위젯으로 구성 |
| 프로젝트 설정 | 팀의 규칙으로 기본 Skill 대체 | 같은 `umg-authoring` 이름의 Skill을 프로젝트 폴더에 배치 |

[Dungeon UI에서 Tool과 Skill을 분리한 과정](Samples/DungeonUi.ko.md#3-tool과-skill의-책임-분리)

## 에이전트가 Skill을 사용하는 흐름

```text
에이전트가 Unreal Editor의 MCP 서버에 연결
  → 서버 안내에서 사용 가능한 Skill 이름·설명을 확인
  → 작업에 맞는 Skill을 skills_get으로 요청
  → SKILL.md 지침과 필요한 참고 파일을 읽음
  → 지침에 따라 실제 MCP Tool 호출
  → 컴파일·PIE·화면 캡처 등으로 결과 확인
```

예를 들어 게임 UI 제작을 시작할 때:

```json
{"name": "umg-authoring"}
```

위 인자로 `skills_get`을 호출하면 `SKILL.md` 본문(앞의 YAML 메타데이터 제외)과 참고 파일 목록을 반환합니다. 별도 참고 파일도 읽을 수 있습니다.

```json
{"name": "perf-investigation", "file": "references/scene-runner.md"}
```

- `skills_list`: 현재 에디터가 제공하는 Skill의 이름·설명·경로, 덮어쓴 원본 경로와 건너뛴 파일의 이유를 조회합니다.
- `skills_get`: Skill 본문이나 같은 폴더 안의 텍스트 참고 파일을 읽습니다. `files`에는 추가 파일 경로가, `bTruncated`에는 응답 잘림 여부가 표시됩니다.
- Skill 내용은 **도구 호출을 안내하는 텍스트**입니다. 읽는 것만으로 위젯·에셋이 생성되거나 변경되지는 않습니다.

## 기본 제공 Skill

| Skill | 작업 내용 |
|---|---|
| [`umg-authoring`](../Plugins/AgentMcp/Skills/umg-authoring/SKILL.md) | 재사용 Widget Blueprint, `BindWidget`, 데이터 기반 목록, PIE 화면 검토 |
| [`ui-style-system`](../Plugins/AgentMcp/Skills/ui-style-system/SKILL.md) | Theme Data Asset, 스타일 토큰, 스타일 추출·적용, UI Kit Gallery |
| [`ui-art-requests`](../Plugins/AgentMcp/Skills/ui-art-requests/SKILL.md) | 이미지 제작 요청·검토, 텍스처 임포트 및 UMG 연결 |
| [`blueprint-graphs`](../Plugins/AgentMcp/Skills/blueprint-graphs/SKILL.md) | Blueprint 클래스·변수·함수·그래프 편집과 실행 확인 |
| [`perf-investigation`](../Plugins/AgentMcp/Skills/perf-investigation/SKILL.md) | 고정 장면 반복 측정, CSV·Insights 분석, 전후 편차 비교 |

성능 Skill에는 에디터가 닫힌 상태에서 독립 실행 게임으로 측정하는 스크립트와 장면 실행기 참고 자료도 포함됩니다. **측정 실행은 스크립트**, **결과 파일의 해석은 MCP 성능 도구**가 맡습니다. [성능 측정 워크플로](Workflows.ko.md#6-성능-측정과-비교)

## 프로젝트에 Skill 추가·수정하기

검색 폴더는 아래 순서입니다. **뒤에 발견된 동일 이름의 Skill이 앞의 Skill을 통째로 대체**합니다. 두 파일의 본문을 자동 병합하지는 않습니다.

1. `Plugins/AgentMcp/Skills` — 플러그인 기본 Skill
2. `<프로젝트>/AgentMcp/Skills` — 프로젝트 전용 Skill
3. `SkillDirectories` — 설정에서 지정한 추가 폴더(나열된 순서대로). 상대 경로는 프로젝트 루트 기준

예를 들어 프로젝트용 새 Skill을 만들려면 `AgentMcp/Skills/studio-ui/SKILL.md`를 추가합니다.

```markdown
---
name: studio-ui
description: Apply this project's UI naming and review rules when editing UMG screens.
---

# Studio UI

1. 기존 Widget Blueprint와 C++ 부모 클래스를 먼저 조사한다.
2. 프로젝트의 컴포넌트·네이밍 규칙을 따른다.
3. 변경 후 컴파일하고 PIE에서 실제 화면을 확인한다.
```

폴더명과 `name`은 같아야 하며, `name`은 소문자 영문·숫자·하이픈으로 작성합니다. `description`은 에이전트가 어떤 작업에 지침을 적용할지 판단하는 설명입니다. 추가 경로는 [설정](Settings.ko.md)의 `SkillDirectories`로 지정할 수 있습니다.

**동적 로딩 확인:** `skills_list`로 `studio-ui`를 찾고 `skills_get`으로 읽은 다음, **에디터를 종료하지 않고** `SKILL.md`의 본문을 수정해 같은 `skills_get`을 다시 호출하면 바뀐 내용이 반환됩니다. 기본 `umg-authoring`을 프로젝트 방식으로 대체하려면 같은 이름의 `AgentMcp/Skills/umg-authoring/SKILL.md`를 둡니다.

**로딩 시점은 구분해야 합니다.** `skills_list`와 `skills_get`은 호출할 때마다 폴더를 재탐색하고 파일을 다시 읽습니다. 반면 **MCP 초기 연결 안내에 포함된 Skill 목록은 초기화 시 생성**됩니다. 새 Skill의 이름·설명을 초기 안내에도 표시하려면 클라이언트를 다시 연결해야 할 수 있습니다.

## Claude Code·Codex에서 발견하기

서버가 알려 주는 Skill과 **클라이언트가 자체적으로 검색하는 Skill**은 별개입니다. 이 저장소에는 각 기본 Skill의 이름과 설명만 가진 짧은 연결용 파일이 있습니다.

- Claude Code: [`.claude/skills`](../.claude/skills)
- Codex: [`.agents/skills`](../.agents/skills)

클라이언트가 작업에 맞는 로컬 Skill을 선택하면, 연결용 파일은 `skills_get`을 호출하도록 안내합니다. 이렇게 실제 작업 지침은 **플러그인의 `SKILL.md` 한 곳**에서 관리합니다. 다른 프로젝트나 클라이언트가 이 연결용 파일을 자동으로 갖는 것은 아니며, 필요한 경우 해당 클라이언트에 맞게 추가해야 합니다.

## 구현 구조

| 구현 | 책임 |
|---|---|
| [`AgentMcpSkills.cpp`](../Plugins/AgentMcp/Source/AgentMcpToolset/Private/AgentMcpSkills.cpp) | `ScanSkills`로 경로 순서대로 폴더를 탐색하고 `ParseSkillFile`로 메타데이터와 본문 파싱 |
| [`AgentMcpSkillTools.cpp`](../Plugins/AgentMcp/Source/AgentMcpTools/Private/Toolsets/AgentMcpSkillTools.cpp) | `skills_list` / `skills_get` 구현, 요청된 파일의 실시간 읽기와 반환 |
| [`AgentMcpSkillTools.h`](../Plugins/AgentMcp/Source/AgentMcpTools/Private/Toolsets/AgentMcpSkillTools.h) | Reflection 기반 MCP Tool 선언과 반환 구조체 정의 |
| [`AgentMcpSettings.h`](../Plugins/AgentMcp/Source/AgentMcpToolset/Public/AgentMcpSettings.h) | `SkillDirectories`와 응답 크기 제한 설정 |

`skills_get`은 Skill 폴더 내부의 상대 경로 텍스트 파일만 제공합니다. `..`을 이용한 상위 경로 접근, 숨김 파일과 바이너리 파일은 거부하며 반환량을 제한합니다.

UE 5.8은 `UAgentSkill`과 `UAgentSkillToolset`을 통한 에셋/클래스 기반 Skill 인터페이스를 제공합니다. AgentMcp의 방식은 별도의 **Markdown 파일 탐색·읽기 구현**입니다. UE 5.8의 공식 Skill 클래스를 이전 버전으로 포팅하거나 동일한 API를 제공하는 것은 아닙니다. [UE 5.8의 `UAgentSkill`](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ToolsetRegistry/UAgentSkill) · [`UAgentSkillToolset`](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ToolsetRegistry/UAgentSkillToolset)
