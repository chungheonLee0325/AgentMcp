# 던전 UI 샘플

[English](DungeonUi.md)

몬스터 수집형 서바이벌 게임 느낌의 던전 진행 HUD와 던전 결과 팝업입니다. Claude Code(데스크톱 앱, 버전 2.1.270)가 Agent MCP
테스트베드에서 `umg` 도구로 만들었고, UMG 디자이너는 열지 않았습니다. 스크린샷만이 아니라 작업 방식도 판단할 수 있도록, 리뷰에서
반려된 첫 버전을 포함해 만든 과정을 기록합니다.

![HUD 위에 뜬 결과 팝업](../Images/dungeon_result.jpg)

![던전 진행 HUD](../Images/dungeon_hud.jpg)

## 구성

| `/Game/Samples/DungeonUi` 아래 에셋 | C++ 부모 클래스 | 역할 |
|---|---|---|
| `Components/WBP_RewardSlot` | `AgentMcpSampleRewardSlot` | 아이콘, 이름, 개수가 있는 보상 슬롯. 입력 `Reward`(아이템 이름, 개수, 등급, 아이콘 모양)가 등급 색과 아이콘 모양을 정함 |
| `Components/WBP_StatTile` | `AgentMcpSampleStatTile` | 큰 값 위에 라벨: `Label`, `Value`, `SetValue` |
| `Components/WBP_ObjectiveRow` | `AgentMcpSampleObjectiveRow` | 체크 표시, 라벨, 완료/전체. 진행 중인 목표는 `AccentColor`, 완료한 목표는 초록색 |
| `WBP_DungeonHud` | `AgentMcpSampleDungeonHud` | 타이머와 진행도가 있는 던전 카드, 목표 행 인스턴스 3개, 보스 체력 바 |
| `WBP_DungeonResult` | `AgentMcpSampleDungeonResult` | 통계 타일 인스턴스 3개와, `Rewards` 항목마다 보상 슬롯을 하나씩 만드는 `DynamicEntryBox`가 있는 결과 카드. 배경, 카드 등장, 랭크 도장, 보상 순차 등장, 레벨업하는 경험치 바 연출을 재생 |
| `WBP_DungeonDemo` | `UserWidget` | 데모 화면: HUD 인스턴스와, 인스턴스 속성 `Rewards`에 데모 보상 5개를 넣은 결과 인스턴스 |

C++ 부모 클래스와 색 토큰(`AgentMcpSampleStyle.h`)은 `Source/AgentMcpTestbed`에 있습니다. 텍스트는 한국어입니다. Roboto에는 한글
글리프가 없어서 엔진의 대체 폰트로 표시됩니다.

## 실행

[테스트베드와 smoke 테스트](../../README.ko.md#테스트베드와-smoke-테스트)대로 테스트베드를 연 뒤 다음 도구를 호출합니다.

```
pie_start
sample_show_widget {"widgetClass": "/Game/Samples/DungeonUi/WBP_DungeonDemo.WBP_DungeonDemo_C"}
viewport_capture
pie_stop
```

`viewport_capture` 전에 등장 연출이 끝나도록 3초쯤 기다립니다.

## 만든 과정

### 첫 버전: 화면마다 평평한 트리 하나

요청은 상용 게임처럼 보이는 HUD와 결과 팝업이었습니다. Claude Code는 데이터와 연출을 맡는 C++ 부모 클래스를 쓰고, 화면마다
`umg_add_widgets` 한 번(위젯 48개, 78개)으로 브러시, 폰트, 슬롯 배치까지 한꺼번에 넣었습니다. 결과를 보면서 문제 두 가지를
찾았습니다.

- 첫 `viewport_capture`에서 진행 바가 파랗게 물들어 있었습니다. `ProgressBar`는 채움에 `FillColorAndOpacity`를 곱하는데 기본값이
  파란색입니다. `umg_set_widget_properties` 한 번으로 흰색으로 바꿨습니다.
- 두 `umg_add_widgets` 결과가 약 3만 자, 5만 2천 자였습니다. 도구가 구조체 값을 전부 되읽었기 때문이고, 두 번째는 Claude Code의
  도구 출력 한도를 넘었습니다. 지금은 요청한 필드만 되읽습니다. 이 수정은 Live Coding으로 넣고 smoke 테스트로 확인했습니다.

그 버전에 대한 사용자 리뷰: 완성돼 보이지만 재사용할 수 있는 것이 하나도 없었습니다. 보상 슬롯 5개, 통계 타일 3개, 목표 행 3개가
번호만 다른 복사본이었고, 브러시 38개를 인라인으로 적었으며, 보상 목록은 데이터를 따라갈 수 없었습니다.

### 고칠 곳

반복 요소를 부품으로 나누는 것은 도구의 성질이 아니라 작업 관례이고, 스튜디오마다 방식이 다릅니다. 그래서 모든 클라이언트가 요청마다
불러오는 도구 설명이 아니라 스킬 [`umg-authoring`](../../Plugins/AgentMcp/Skills/umg-authoring/SKILL.md)에 넣었습니다. 도구 설명에는
엔트리 클래스로 위젯 블루프린트를 쓸 수 있다는 식의, 도구가 하는 일과 스킬 이름만 적습니다. 이 스킬은 처음에 `.claude/skills`의
Claude Code 스킬이었고, 지금은 플러그인이 `skills_get`으로 제공해서 Codex와 다른 MCP 클라이언트도 같은 내용을 읽습니다.

### 두 번째 버전: 부품, 데이터, 데모 화면

스킬을 따라 진행했습니다.

1. **계획.** 부품: 보상 슬롯, 통계 타일, 목표 행. 데이터: `Reward` 구조체, 라벨과 값, 완료/전체. 동적 목록: 보상. 스타일 토큰:
   `AgentMcpSampleStyle.h`.
2. **부품의 C++ 부모 클래스.** `BindWidget` 규칙, `NativePreConstruct`에서 적용하는 인스턴스 편집 가능 입력, setter를 두었습니다.
   화면 부모 클래스는 부품을 클래스로 바인딩하고 데이터를 넘깁니다. 새 클래스라서 에디터를 닫고 빌드해야 했습니다.
3. **부품.** 루트 위젯을 규칙에 맞는 이름으로 추가할 수 있도록 루트 없이 `umg_create_widget_blueprint`를 호출하고, 부품마다
   `umg_add_widgets` 한 번, 그다음 `blueprint_compile`.
4. **화면.** `umg_remove_widgets` dry run이 복사된 하위 트리(HUD 19개, 팝업 48개)를 보여 주고, `TimeTile`을 지우면 대체 위젯이 생길
   때까지 필수 `BindWidget`이 비게 된다고 경고했습니다. 확인 호출로 삭제한 뒤, `umg_add_widgets`로 라벨을 인스턴스 속성으로 넣은
   부품 인스턴스와, `EntryWidgetClass`가 보상 슬롯인 `DynamicEntryBox`를 넣었습니다.
5. **데모 화면.** HUD 인스턴스와 결과 인스턴스를 넣고, 인스턴스 속성 `Rewards`에 등급과 아이콘 모양이 다른 보상 5개를 넣었습니다.
6. **검토.** 모든 위젯 블루프린트를 부품부터 `blueprint_compile`하고, HUD 단독 화면과 데모 화면을 `pie_start`, `sample_show_widget`,
   `viewport_capture`, `pie_stop`으로 확인한 뒤 `asset_save`.

smoke 테스트에도 같은 경로의 검사를 추가했습니다. 인스턴스 속성을 설정한 위젯 블루프린트 인스턴스, `DynamicEntryBox` 엔트리 클래스,
위젯 블루프린트를 자기 자신 안에 넣는 요청의 거부입니다.

## 다루지 않은 것

- 연출은 C++ 부모 클래스의 코드입니다. 도구로 UMG 위젯 애니메이션을 만들 수는 없습니다.
- 아이콘은 텍스처가 아니라 브러시 모양입니다.
- 도구로 위젯을 다른 부모로 옮길 수 없어서, 재구성할 때 복사본을 지우고 인스턴스를 새로 넣었습니다.
