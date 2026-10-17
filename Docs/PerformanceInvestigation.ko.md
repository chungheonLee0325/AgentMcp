# UI 성능 조사 — 반복 측정과 CPU/GPU 원인 분석

[English](PerformanceInvestigation.md) · [문서 목차](Index.ko.md) · [`perf-investigation` Skill](../Plugins/AgentMcp/Skills/perf-investigation/SKILL.md)

AgentMcp는 **게임의 성능 측정 장면을 실행·비교하고, 특정 UI 조작의 병목을 찾는 과정**을 Skill, 실행 스크립트, MCP 분석 Tool로 나눴습니다. 평균 FPS만 확인하는 대신 **화면 열기, 아이템 교환, 드래그처럼 특정 프레임에 발생하는 비용**을 측정 대상으로 삼습니다.

이 기능을 적용한 실제 사례는 [Sonheim UI 성능 최적화 PR #6](https://github.com/chungheonLee0325/Sonheim/pull/6)입니다. **AgentMcp 저장소에는 재사용할 조사 도구를**, **Sonheim 저장소에는 프로젝트 전용 측정 장면과 최적화 코드를** 구현했습니다. 이 문서는 전자를 중심으로 설명합니다.

## 조사 흐름과 구성 요소

```text
프로젝트별 측정 장면 구현 (CSV_EVENT + CPU Trace Scope)
    → preflight.py : 머신 상태·실행 중 에디터 확인
    → run.py       : 워밍업 + 독립 실행 게임 반복 측정
    → perf_summarize_csv : 평상시 프레임과 조작 순간의 비용 확인
    → run.py --trace → perf_trace_hotspots : CPU 병목 원인 추적
                  └─ ProfileGPU 로그 → perf_read_gpu_profile : GPU 패스 확인
    → 원인 코드 수정
    → 같은 조건에서 run.py 재측정
    → perf_compare_csv : 전후 변화와 실행 간 편차 확인
    → 실제 플레이·화면·메모리 영향 검토
```

| 구성 | 담당 | 코드·자료 |
|---|---|---|
| `perf-investigation` Skill | 조사 순서, 측정 규칙, 결과 해석 기준 | [SKILL.md](../Plugins/AgentMcp/Skills/perf-investigation/SKILL.md) |
| `preflight.py` | 에디터·빌드·백그라운드 부하 점검, 종료 대상의 승인 확인 | [preflight.py](../Plugins/AgentMcp/Skills/perf-investigation/scripts/preflight.py) |
| `run.py` | 독립 실행 반복, 워밍업, 부하 샘플링, 유효 실행 선별 | [run.py](../Plugins/AgentMcp/Skills/perf-investigation/scripts/run.py) |
| 프로젝트별 scene runner | 실제 UI 조작 재현, 이벤트·트레이스 마커 기록, CSV 저장 | [구현 계약·예제](../Plugins/AgentMcp/Skills/perf-investigation/references/scene-runner.md) |
| `perf_*` MCP Tools | 생성된 CSV·`.utrace`·로그 분석 | [AgentMcpPerfTools.cpp](../Plugins/AgentMcp/Source/AgentMcpTools/Private/Toolsets/AgentMcpPerfTools.cpp) |

**측정과 분석의 실행 시점이 다릅니다.** `run.py`는 같은 프로젝트의 Unreal Editor가 열려 있으면 측정을 시작하지 않습니다. 에디터와 게임이 GPU·CPU 자원을 경쟁하지 않도록 독립 실행 게임을 측정한 뒤, **에디터를 다시 열고 MCP Tool로 생성된 파일을 분석**합니다.

## 1. 프로젝트별로 동일한 조작 재현하기

AgentMcp가 임의의 게임에서 인벤토리를 여는 방법을 자동으로 아는 것은 아닙니다. 프로젝트가 직접 구현하는 **scene runner**가 특정 장소·카메라·데이터 상태를 설정하고 UI 조작을 반복해야 합니다.

[scene-runner.md](../Plugins/AgentMcp/Skills/perf-investigation/references/scene-runner.md)는 개발용 `UGameInstanceSubsystem` 예시와 다음 계약을 제공합니다.

- `-PerfScenes`로 측정 장면을 선택하며, 일반 플레이에서는 실행하지 않습니다.
- `-PerfLabel`, `-PerfRun`으로 실행을 구분하고, `Saved/Perf/<label>/csv/<scene>_r<n>.csv` 형식으로 저장합니다.
- 장면 전환 후 일정 시간 안정화한 다음 CSV 수집을 시작하고, **비동기 CSV 쓰기 완료를 다음 프레임에서 확인**한 뒤 종료합니다.
- 조작이 시작되는 위치에 `CSV_EVENT`와 CPU Trace Scope를 함께 기록합니다.

```cpp
// 실제 프로젝트의 측정 대상 작업에 추가하는 마커 예시
CSV_DEFINE_CATEGORY(PerfUI, true); // Define once in a suitable source file

// In the function that performs the interaction:
CSV_EVENT(PerfUI, TEXT("Open Inventory"));
TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(TEXT("PerfUI Open Inventory"));
OpenInventory();
```

`CSV_EVENT`는 **조작이 발생한 프레임**을 찾기 위한 것이고, `TRACE_CPUPROFILER_EVENT_SCOPE_TEXT`는 **그 작업을 둘러싼 함수의 실행 비용**을 찾기 위한 것입니다. 네트워크 응답을 기다리는 조작이라면 입력 시점이 아닌 **실제 UI 갱신이 발생하는 콜백**에 마커를 둬야 합니다.

## 2. 워밍업과 유효 실행 선별

`preflight.py`는 실행 중인 에디터, 빌드·ShaderCompileWorker, CPU 부하가 큰 앱을 검사합니다. 다른 앱을 **자동으로 무조건 종료하지 않습니다.** 필요한 경우 사용자가 지정한 `--close` 대상만 처리합니다. 작업 중인 프로그램이 있다면 닫아도 되는지 먼저 확인해야 합니다.

`run.py`는 Python과 `psutil`이 필요합니다. 설치 및 준비가 끝난 뒤 다음은 **셸에서 실행하는 명령**입니다. MCP Tool 호출이 아닙니다.

```text
python Plugins/AgentMcp/Skills/perf-investigation/scripts/run.py --uproject "<PROJECT.uproject>" --label before --map /Game/Maps/Main --runs 3 --game-args "-PerfScenes=OpenClose,InvSwap -PerfSeconds=10 -PerfWarmup=3 -PerfLabel={label} -PerfRun={run}"
```

`/Game/Maps/Main`과 `OpenClose,InvSwap`은 예시입니다. 프로젝트가 제공하는 맵·장면 이름으로 바꿔야 합니다. `run.py`가 장면을 구현하거나 UI 이벤트를 자동으로 생성하지는 않습니다.

실행기는 워밍업 1회를 버리고, 후속 실행에서 다른 프로세스의 CPU 부하를 1초 간격으로 샘플링합니다. 평균 부하가 기준을 넘은 실행은 무효 처리하고 재실행합니다(최대 요청 횟수의 두 배 시도). `runs.json`의 `invalid` 목록은 두 CSV 분석 도구가 제외합니다. 개발 빌드의 `ensure`도 별도 경고로 남깁니다.

`GameUserSettings.ini`를 실행마다 복원해 이전 실험의 `sg.*` 변경이 다음 측정에 남지 않게 합니다. 해상도, 그래픽 설정, 코드 버전, 맵, 이벤트 데이터를 전후 동일하게 맞춰야 합니다.

## 3. CSV로 지연 순간과 평상시 프레임을 구분

에디터를 다시 연 다음 아래 인자로 `perf_summarize_csv`를 호출합니다.

```json
{"folder": "Saved/Perf/before/csv"}
```

`<scene>_r<n>.csv`들을 같은 장면으로 묶습니다. 코드가 반환하는 통계의 의미는 다음과 같습니다.

| 결과 필드 | 의미 |
|---|---|
| `stats[].median` | **각 실행의 평균값**을 구한 후, 실행들 사이의 중앙값 |
| `stats[].spread` | 실행별 평균값의 최댓값 − 최솟값 |
| `stats[].p95` | 각 실행의 95백분위 값을 구한 후 그 중앙값 |
| `events[].frameMedian` | `CSV_EVENT` 발생 프레임부터 지정한 창(기본 3프레임)에서 가장 긴 프레임을 구하고, 이벤트 발생별로 중앙값 산출 |
| `events[].gameThreadMedian` | 같은 방식으로 구한 Game Thread 시간의 중앙값 |
| `events[].frameMax` | 이벤트 관측치 중 가장 큰 프레임 시간 |

평균 프레임은 **상시 비용**, 이벤트 값은 **열기·교환·드래그 순간의 지연**을 설명합니다. 두 값을 혼용하거나 이벤트 프레임의 중앙값을 평균 FPS 개선으로 표현하지 않습니다.

CSV는 Unreal CSV Profiler가 실행 도중 열을 추가할 수 있어 **파일 끝에 기록된 전체 헤더**를 기준으로 행을 읽습니다. 이 방식은 앞부분 헤더만 보고 프레임 일부를 누락하는 문제를 피하기 위한 구현입니다.

## 4. Unreal Insights / GPU 원인 분석

CPU 병목을 찾을 때는 숫자를 다시 재는 것이 아니라, 같은 조작이 어느 함수에서 비용을 발생시키는지 조사합니다.

```text
python Plugins/AgentMcp/Skills/perf-investigation/scripts/run.py --uproject "<PROJECT.uproject>" --label trace --map /Game/Maps/Main --trace --game-args "-PerfScenes=OpenClose,InvSwap -PerfSeconds=10 -PerfWarmup=3 -PerfLabel={label} -PerfRun={run}"
```

Trace가 생성되고 에디터를 다시 연 뒤 `perf_trace_hotspots`를 호출합니다.

```json
{"traceFile": "Saved/Perf/trace/run.utrace", "markerPrefix": "PerfUI ", "thread": "GameThread", "top": 20}
```

이 Tool은 필요 시 `UnrealInsights.exe`를 **UI 없이 실행**해 스레드 타이밍 이벤트를 파일로 내보낸 뒤, 마커가 속한 프레임과 후속 프레임의 타이머를 분석합니다. `selfMs`(자기 시간), `inclusiveMs`(하위 호출 포함), `caller`, `maxSelfMs` 등을 제공합니다. 최초 로딩의 영향을 분리할 때는 `bSkipFirst`를 사용할 수 있습니다.

GPU 원인은 게임에서 `ProfileGPU`를 실행해 로그를 남긴 후 `perf_read_gpu_profile`로 패스 계층을 읽습니다.

```json
{"logFile": "Saved/Perf/before/game_r1.log", "minMs": 0.1, "maxDepth": 4}
```

위 로그 예시는 **해당 실행에서 실제로 `ProfileGPU` 덤프를 기록한 경우**에만 유효합니다.

**Trace와 ProfileGPU는 원인을 찾는 자료이지, 개선 효과를 주장할 기준 측정값이 아닙니다.** `-statnamedevents`와 GPU 프로파일링 자체에도 오버헤드가 있으므로, 최종 성능 변화는 별도 CSV 반복 측정으로 확인합니다.

## 5. 전후 비교와 채택 기준

변경 후 같은 장면·환경에서 `--label after`로 재측정하고 다음 MCP Tool을 호출합니다.

```json
{"baselineFolder": "Saved/Perf/before/csv", "candidateFolder": "Saved/Perf/after/csv"}
```

`perf_compare_csv`는 장면/컬럼별 **개선 후 − 개선 전** 값을 반환합니다. `stats[].bBeyondNoise`는 변화 절댓값이 전후 양쪽 `spread`보다 모두 큰지를 나타냅니다. **이는 실행 간 변동폭을 이용한 필터이지 통계적 유의성 검정은 아닙니다.** 또한 이벤트 중앙값의 전후 차이(`events[]`)는 반환하지만, **이벤트별 `bBeyondNoise` 판정은 제공하지 않습니다.** 이벤트 개선을 해석할 때는 각 측정의 반복 횟수·분포·환경을 같이 검토해야 합니다.

Skill은 변화가 0.5ms 미만이면 전후 6회씩 측정하도록 안내합니다. 유의미하지 않은 최적화는 채택하지 않을 수 있으며, 위젯 재사용처럼 CPU 시간과 메모리가 교환되는 경우 메모리 비용까지 함께 확인합니다.

## 실제 적용 — Sonheim UI

[Sonheim의 측정 장면 구현](https://github.com/chungheonLee0325/Sonheim/blob/feat-ui-perf/Sonheim/Source/Sonheim/Utilities/PerfScenarioSubsystem.cpp)은 메뉴 재오픈, 인벤토리 교환, 드래그·드롭 등 실제 게임의 조작 경로를 재현합니다. 다음은 [PR #6](https://github.com/chungheonLee0325/Sonheim/pull/6)과 [Wiki 20장](https://github.com/chungheonLee0325/Sonheim/blob/feat-ui-perf/Doc/Wiki/20_UI_Performance_Profiling.md)에 기록된 **1920×1080, RTX 2070 SUPER, 조작 이벤트별 3회 실행 중앙값**입니다.

| 조작 | 개선 전 Frame (Game Thread) | 개선 후 Frame (Game Thread) |
|---|---:|---:|
| 아이템 교환 | 32.6ms (19.4ms) | **17.1ms (3.0ms)** |
| 인벤토리 열기 | 57.5ms (44.6ms) | **22.1ms (8.2ms)** |

![Sonheim UI 조작 프레임 비교 그래프](https://github.com/chungheonLee0325/Sonheim/blob/feat-ui-perf/Doc/Media/Wiki/20_UI_Performance_Profiling/20_interaction_frame_comparison.svg?raw=true)

실측한 지연을 Insights로 추적해 인벤토리 갱신 때 슬롯·툴팁을 반복 구성하는 비용, 화면을 열 때마다 다시 발생하는 위젯 생성 비용 등을 찾았습니다. Sonheim 쪽에서 실제 슬롯 갱신·UIStack·Delegate/Render Target 수명을 수정했습니다. **이 수치는 Sonheim의 특정 장면에서 확인한 최적화 결과**이며 AgentMcp 플러그인만 설치하면 게임 성능이 자동으로 개선된다는 뜻이 아닙니다.

## 자료와 제약

- 측정은 **실행 가능한 Development 게임과 프로젝트 전용 scene runner**가 전제입니다. Skill 및 Tool만으로 측정 장면이 만들어지지 않습니다.
- 게임의 실제 UI 경로, 입력 타이밍, 해상도, 첫 열기/재진입 조건이 다르면 결과도 달라집니다.
- 인게임 첫 생성 비용과 재사용 UI 비용은 구분하고, 듣고 보이는 결과가 바뀌지 않았는지도 확인합니다.
- 에디터가 실행 중일 때는 `run.py` 측정을 시작할 수 없습니다. 분석 Tool은 에디터를 다시 연 뒤 프로젝트 내부 결과 파일을 읽습니다.
- `perf_trace_hotspots`는 Unreal Insights 실행 파일과 CPU Trace가 필요하며, `perf_read_gpu_profile`에는 `ProfileGPU`를 기록한 로그가 필요합니다.
- **원본 MCP 도구 결과 스크린샷은 아직 별도 확보하지 않았습니다.** 위 그래프는 Sonheim에서 측정값으로 만든 비교 차트이며 Unreal Insights 화면 캡처가 아닙니다.

[Tool별 입력·출력 선언](../Plugins/AgentMcp/Source/AgentMcpTools/Private/Toolsets/AgentMcpPerfTools.h) · [작업별 MCP 워크플로](Workflows.ko.md#6-성능-측정과-비교) · [Sonheim 원인 분석과 UI 구현](https://github.com/chungheonLee0325/Sonheim/blob/feat-ui-perf/Doc/Wiki/20_UI_Performance_Profiling.md)
