# 작업별 MCP 워크플로

[English](Workflows.md) · [문서 목차](Index.ko.md)

아래 예제는 도구를 **어떤 순서로 호출하는지** 보여 줍니다. 모든 명령은 에디터에 연결된 Claude Code나 Codex가 MCP 도구로 실행하는 것이며, 셸 명령이 아닙니다. 구체적인 인자와 접근 수준은 [도구 문서](Tools.ko.md)를 참고하세요.

어느 작업이든 먼저 `editor_get_state`로 **열린 프로젝트, 레벨, PIE 상태, 저장되지 않은 변경**을 확인합니다.

## 1. 블루프린트 수정 전에 영향 범위 파악

```text
editor_get_state
  → asset_find / blueprint_inspect
  → blueprint_find_usages
  → blueprint_get_graph 또는 blueprint_read_graph
  → 변경 대상과 호출 지점 검토
```

- `blueprint_find_usages`는 Find in Blueprints 인덱스에서 함수 호출, 변수 읽기/쓰기, 캐스트, 이벤트 바인딩 위치를 찾습니다. 로드되지 않은 블루프린트도 검색 대상에 포함됩니다.
- 그래프 전체가 길다면 `blueprint_get_graph`의 진입점·연결 체인 조회 또는 `blueprint_read_graph`를 활용합니다.
- 검색 결과만으로 수정하지 말고 대상 그래프와 참조 지점을 확인합니다.

## 2. Widget Blueprint 제작 및 화면 확인

```text
umg_inspect (기존 화면인 경우)
  → umg_create_widget_blueprint
  → umg_add_widgets / umg_set_widget_properties
  → blueprint_compile
  → pie_start
  → viewport_capture
  → log_get_recent
  → pie_stop
  → asset_save
```

- 기존 위젯을 삭제한다면 `umg_remove_widgets`로 먼저 dry run을 확인하고, 영향이 의도한 범위인지 검토한 후 `bConfirm`을 설정합니다.
- C++ 부모 위젯이 있다면 `BindWidget` / `BindWidgetOptional` 이름과 타입을 `umg_inspect`로 확인합니다.
- 반복 항목은 복제된 트리보다 Widget Blueprint 컴포넌트와 데이터 기반 목록으로 구성하는 것이 좋습니다. 구체적인 제작 규칙은 [UMG 샘플](Samples/DungeonUi.ko.md) 및 `umg-authoring` Skill에 있습니다.
- 결과는 컴파일 성공만으로 판단하지 않고 PIE 캡처와 로그를 같이 봅니다.

## 3. 블루프린트 그래프 편집

```text
blueprint_inspect
  → blueprint_get_graph
  → blueprint_find_node_types
  → blueprint_add_members (필요한 경우)
  → blueprint_edit_graph
  → blueprint_compile
  → pie_start → log_get_recent → pie_stop
  → asset_save
```

- 노드 타입은 추측해 넣기보다 `blueprint_find_node_types`로 에디터가 제공하는 타입과 연결 가능한 핀을 조회합니다.
- 그래프 편집은 한 호출을 하나의 Undo 단계로 처리하며, 호출 내부의 실패 시 롤백합니다.
- 변수·함수·이벤트 디스패처의 추가는 지원하지만, README에 적힌 일부 삭제·이름 변경 및 자동 배치 기능은 제공하지 않습니다.

## 4. C++ 또는 설정 파일 조사와 수정

```text
source_find / source_search
  → source_read
  → 수정 내용 검토
  → source_replace
  → livecoding_compile (적용 가능한 변경일 때)
  → log_get_recent
```

- 소스 도구는 설정된 `SourceFolders` 안의 텍스트 파일만 대상으로 하며, UTF-8·UTF-16·CP949를 처리합니다.
- `source_replace`와 `source_write`는 에디터 Undo 대상이 아닙니다. 변경 전에 버전 관리 상태를 확인하세요.
- Live Coding으로 반영할 수 없는 `UCLASS`, `USTRUCT`, `UPROPERTY`, `UFUNCTION` 선언 변경은 정식 빌드가 필요합니다.

## 5. 레벨·에셋을 조사하고 담당자에게 보여 주기

```text
asset_find / actor_find
  → asset_inspect / actor_inspect
  → editor_show_in_content_browser 또는 editor_open_assets
  → actor_select 또는 viewport_set_camera
```

조회·선택·화면 이동은 콘텐츠 자체를 편집하지 않습니다. 에셋 값 변경에는 `object_list_properties` / `object_get_properties`를 먼저 이용해 대상 프로퍼티를 확인한 후 `object_set_properties`를 사용합니다.

## 6. 성능 측정과 비교

```text
에디터 종료
  → perf-investigation Skill의 run.py로 고정 장면 반복 실행
  → perf_summarize_csv (CSV 결과)
  → perf_trace_hotspots (필요한 경우 Insights 트레이스)
  → perf_read_gpu_profile (필요한 경우 ProfileGPU 로그)
  → 코드/에셋 수정
  → 같은 조건에서 다시 실행
  → perf_compare_csv
```

- `perf-investigation` Skill은 독립 실행 게임의 반복 측정, 워밍업, 시스템 부하 점검, 방해받은 실행의 재시도를 안내합니다.
- `perf_summarize_csv`는 실행 묶음의 프레임과 표시한 이벤트를 정리합니다. `perf_compare_csv`는 두 묶음의 변화가 실행 간 편차를 넘는지 보여 줍니다.
- 문제가 발생하는 순간은 `CSV_EVENT`로 표시하고, 원인 분석이 필요하면 Insights 트레이스의 타이머별 시간을 확인합니다.
- 수치는 장면, 해상도, 빌드, 실행 조건을 함께 기록해야 비교할 수 있습니다. 한 번의 측정 차이를 성능 향상이라고 단정하지 않습니다.
- 실행 스크립트, MCP 분석 도구의 책임 분리와 결과 필드의 의미는 [성능 분석 구현과 Sonheim 사례](PerformanceInvestigation.ko.md)를 참고하세요.

## 외부 MCP 클라이언트와 에디터 채팅의 차이

에디터 내 [AI 채팅](AiChat.ko.md)은 권한 모드와 변경 승인 카드를 제공합니다. 직접 연결한 Claude Code·Codex에는 같은 승인 카드가 자동으로 적용되지 않습니다. 연결 방식과 제한을 확인하려면 [클라이언트 연결](Clients.ko.md), [안전장치](Safety.ko.md)를 참고하세요.
