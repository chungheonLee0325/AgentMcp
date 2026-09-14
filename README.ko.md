# Unreal Engine 5.5용 Agent MCP

[English](README.md)

Agent MCP는 언리얼 에디터 안에서 [Model Context Protocol](https://modelcontextprotocol.io) 서버를 실행합니다. Claude Code
같은 코딩 에이전트가 프로젝트를 조사하고, 수정하고, 실행한 뒤 결과를 확인할 수 있습니다.

- 레벨, 액터, 프로퍼티, 에셋, 블루프린트, 위젯 블루프린트, DataTable 조회
- 액터 프로퍼티와 DataTable 행을 되돌릴 수 있는 에디터 트랜잭션으로 변경하고, 에셋은 명시적으로 저장
- 블루프린트 컴파일, Live Coding으로 C++ 컴파일, Play In Editor 시작과 종료
- 에디터 로그 읽기, 뷰포트 캡처(플레이 세션의 게임 UI 포함)

도구는 일반 `static UFUNCTION`입니다. 이름, 설명, JSON 스키마를 리플렉션에서 만들기 때문에 함수 하나를 추가하면 도구
하나가 생깁니다.

> **상태: 베타.** Windows 64비트의 Unreal Engine 5.5.4(설치형 빌드)로 빌드하고 테스트했습니다. 이 저장소의 smoke 테스트는
> 테스트베드 프로젝트에서 114개 검사를 통과합니다. 다른 엔진 버전과 플랫폼은 확인하지 않았습니다.

- [설치](#설치)
- [클라이언트 연결](#클라이언트-연결)
- [도구](#도구)
- [안전장치](#안전장치)
- [설정](#설정)
- [도구 만들기](#도구-만들기)
- [테스트베드와 smoke 테스트](#테스트베드와-smoke-테스트)
- [한계](#한계)
- [라이선스](#라이선스)

## 설치

1. `Plugins/AgentMcp` 폴더를 프로젝트의 `Plugins` 폴더에 복사합니다.
2. **Edit > Plugins**에서 plugin을 켜거나, `.uproject` 파일에 추가합니다.
   ```json
   "Plugins": [ { "Name": "AgentMcp", "Enabled": true } ]
   ```
3. 프로젝트의 에디터 타깃을 빌드하고(plugin은 소스 코드로 배포됩니다) 에디터를 엽니다.

에디터 로딩이 끝나면 출력 로그에 다음 줄이 나옵니다.

```
LogAgentMcpProtocol: Agent MCP server listening on http://127.0.0.1:18765/mcp (31 tools).
```

## 클라이언트 연결

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

서버는 `initialize` 응답으로 짧은 사용 안내를 보냅니다. 에이전트는 `editor_get_state`로 작업을 시작하는 것이 좋습니다.

**에디터마다 포트 하나.** plugin을 켠 에디터는 모두 설정된 포트를 씁니다. 에디터 두 개를 동시에 실행하면 두 번째
에디터는 포트를 열지 못해 `Agent MCP server failed to start`를 기록하고 도구를 제공하지 않습니다. 동시에 여는 프로젝트에는
서로 다른 포트를 지정하고, 무언가를 바꾸기 전에 `editor_get_state`의 `project` 값을 확인하세요.

## 도구

**Read** 도구는 부작용이 없습니다. **Write** 도구는 되돌릴 수 있는 에디터 트랜잭션 안에서 실행되고, Play In Editor 중에는
거부됩니다. **Destructive** 도구는 `bConfirm`이 true가 아니면 무엇을 할지 보고만 하는 Write 도구입니다. **Control** 도구는
플레이 세션, 컴파일, 저장처럼 되돌릴 수 없는 에디터 상태를 바꿉니다.

| 도구 | 접근 | 설명 |
|---|---|---|
| `editor_get_state` | Read | 열린 레벨, 플레이 세션, 저장 안 된 패키지, 선택, undo 기록, 서버 상태 |
| `editor_undo`, `editor_redo` | Control | 마지막 에디터 트랜잭션 되돌리기와 다시 실행 |
| `log_get_recent` | Read | 시퀀스 번호 이후의 에디터 로그. 로그 수준, 카테고리, 텍스트로 거르기 |
| `actor_find` | Read | 레이블이나 이름, 클래스, 태그, 폴더, 선택으로 액터 찾기(에디터 또는 플레이 월드) |
| `actor_inspect` | Read | 액터의 클래스, 블루프린트, 태그, 트랜스폼, 부착 관계, 컴포넌트 |
| `actor_set_transform` | Write | 액터 이동, 회전, 크기 조절 |
| `object_list_properties` | Read | 객체의 프로퍼티와 변경 가능 여부 |
| `object_get_properties` | Read | 프로퍼티 값을 JSON으로 읽기 |
| `object_set_properties` | Write | 에디터 레벨의 액터나 컴포넌트 프로퍼티 변경과 변경 후 값 확인 |
| `pie_start`, `pie_stop` | Control | Play In Editor를 시작하거나 종료하고, 세션이 시작되거나 끝날 때까지 대기 |
| `pie_status` | Read | 플레이 세션이 시작 중인지, 실행 중인지 |
| `asset_find` | Read | 폴더, 클래스, 이름으로 에셋 찾기(에셋을 로드하지 않음) |
| `asset_inspect` | Read | 에셋의 클래스, 태그, 파일 크기, 로드·변경 상태, 참조 수 |
| `asset_referencers`, `asset_dependencies` | Read | 에셋을 참조하는 패키지, 에셋이 의존하는 패키지 |
| `asset_save` | Control | 로드된 프로젝트 에셋을 대화상자 없이 저장 |
| `class_find_derived` | Read | 클래스를 상속하는 C++·블루프린트 클래스와 헤더, 에셋 |
| `datatable_get_schema` | Read | 행 구조체, C++ 헤더, 열, C++ 타입, JSON 스키마 |
| `datatable_list_rows`, `datatable_get_rows` | Read | 행 이름, 행과 열로 정리한 값 |
| `datatable_set_rows`, `datatable_add_rows`, `datatable_rename_rows` | Write | 프로젝트 DataTable 행 변경, 추가, 이름 변경 |
| `datatable_remove_rows` | Destructive | 프로젝트 DataTable 행 삭제 |
| `blueprint_inspect` | Read | 부모 클래스 체인, 인터페이스, 컴포넌트, 변수, 함수, 그래프 |
| `blueprint_compile` | Control | 블루프린트나 위젯 블루프린트를 컴파일하고 오류와 경고 반환 |
| `umg_inspect` | Read | 슬롯을 포함한 위젯 트리, BindWidget 프로퍼티, 애니메이션, 프로퍼티 바인딩 |
| `viewport_capture` | Read | 레벨 뷰포트나 플레이 세션의 PNG 캡처(게임 UI 포함) |
| `livecoding_compile` | Control | 바뀐 C++를 Live Coding으로 컴파일하고 결과까지 대기 |

일반적인 검증 흐름: `blueprint_compile` → `pie_start` → 반환된 `startLogSequence`부터 `log_get_recent` → `viewport_capture`
→ `pie_stop`

## 안전장치

- **로컬 전용.** 서버는 `127.0.0.1`에서만 연결을 받습니다. 브라우저의 `Origin`이 `localhost`, `127.0.0.1`, `[::1]`이
  아니면 요청을 거부하므로 웹 페이지가 에디터에 접근할 수 없습니다. `AuthToken`을 설정하면 bearer 토큰도 요구합니다.
- **되돌릴 수 있고 전부 아니면 전무인 쓰기.** Write 도구는 무언가를 바꾸기 전에 모든 값을 검사하고, 에디터 트랜잭션
  안에서 실행됩니다. 일부를 바꾼 뒤 실패하면 트랜잭션을 되돌립니다.
- **플레이 중 쓰기 금지.** Play In Editor가 시작 중이거나 실행 중이면 Write 도구를 거부합니다.
- **명시적 저장.** 부수 효과로 저장하는 도구는 없습니다. `asset_save`는 로드된 프로젝트 에셋만 저장하고, 레벨과
  프로젝트 밖의 콘텐츠는 거부합니다.
- **dry run.** Destructive 도구는 `bConfirm: true`가 있어야 실제로 실행합니다.
- **허용·차단 목록.** `AllowedTools`와 `BlockedTools`로 도구를 숨기고, `BlockedProperties`로 `object_set_properties`가
  바꾸지 못할 프로퍼티를 지정합니다.
- 콘솔 명령이나 스크립트를 실행하는 도구는 없습니다.

## 설정

프로젝트의 `Config/DefaultEditorPerProjectUserSettings.ini`에 값을 적습니다.

```ini
[/Script/AgentMcpToolset.AgentMcpSettings]
Port=18765
+BlockedTools=datatable_remove_rows
```

| 키 | 기본값 | 의미 |
|---|---|---|
| `bAutoStartServer` | `True` | 에디터 로딩이 끝나면 서버 시작 |
| `Port` | `18765` | 루프백 포트 |
| `UrlPath` | `/mcp` | 엔드포인트 경로 |
| `AuthToken` | 비어 있음 | 설정하면 클라이언트가 `Authorization: Bearer <token>`을 보내야 함 |
| `ExposureMode` | `Native` | `Native`는 모든 도구를 등록. `ToolSearch`는 `toolsets_list`, `toolsets_describe`, `tools_call`만 등록 |
| `BlockedTools`, `AllowedTools` | 비어 있음 | 도구 이름 와일드카드(예: `datatable_*`) |
| `bAllowWritesDuringPIE` | `False` | Play In Editor 중 Write 도구 허용 |
| `BlockedProperties` | 비어 있음 | `object_set_properties`가 거부할 `ClassName.PropertyName` 와일드카드 |
| `BusyWaitTimeoutSeconds` | `10` | 에디터가 저장, 가비지 수집, 에셋 로딩 중일 때 호출이 기다리는 시간 |
| `MaxResultBytes` | `65536` | 이 크기를 넘는 결과 텍스트는 잘림 |
| `LogBufferLines` | `20000` | `log_get_recent`용으로 보관하는 로그 줄 수 |

## 도구 만들기

에디터 모듈의 의존성에 `AgentMcpToolset`을 추가하고, `UAgentMcpToolset` 하위 클래스에 static 함수를 선언합니다.

```cpp
#include "AgentMcpToolset.h"

#include "MyTools.generated.h"

USTRUCT(BlueprintType)
struct FMyGreeting
{
	GENERATED_BODY()

	UPROPERTY()
	FString Message;
};

/** Example tools. */
UCLASS(meta = (McpToolset = "my"))
class UMyTools : public UAgentMcpToolset
{
	GENERATED_BODY()

public:
	/**
	 * Greets someone.
	 * @param Name Who to greet.
	 * @return The greeting.
	 */
	UFUNCTION(BlueprintCallable, Category = "My Tools", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FMyGreeting Greet(const FString& Name = TEXT("world"));
};
```

이 함수는 선택 문자열 인자 `name`을 받는 `my_greet` 도구가 됩니다.

- 툴셋 클래스는 자동으로 찾습니다. 도구 이름은 `<McpToolset>_<snake_case 함수 이름>`이고, 주석이 설명이 됩니다.
- `McpAccess`는 `Read`, `Write`, `Destructive`, `Control` 중 하나입니다. 지정하지 않으면 `Write`로 취급합니다.
- `BlueprintCallable`을 빼지 마세요. Unreal Engine 5.5는 블루프린트에서 호출 가능한 함수에만 C++ 기본 인자 값을 기록하고,
  기본값이 없으면 모든 인자가 필수가 됩니다. `BlueprintInternalUseOnly`는 함수가 블루프린트 메뉴에 나오지 않게 합니다.
- `USTRUCT(BlueprintType)`를 반환하세요. 필드가 JSON 결과가 되고, 빈 문자열·배열·구조체는 결과에서 빠집니다.
- 실패는 `UE::AgentMcp::RaiseToolError(TEXT("CODE"), TEXT("Message"), TEXT("Hint"))`로 알리고 반환합니다.
- 객체 인자(`UObject*`, `AActor*`, `UClass*`)는 오브젝트 경로를 받고, 액터는 레이블도 받습니다.
- 다음 프레임 이후에 끝나는 작업은 `UAgentMcpAsyncResult::Create(TimeoutSeconds, PollFunction)`를 반환합니다. Read와
  Control 도구만 가능합니다.
- 이미지를 반환하려면 결과 구조체에 `FAgentMcpImage` 필드를 추가합니다.

## 테스트베드와 smoke 테스트

저장소 루트는 plugin을 빌드하고 테스트하는 작은 Unreal Engine 5.5 프로젝트입니다.

| 경로 | 내용 |
|---|---|
| `Plugins/AgentMcp` | plugin |
| `Source/AgentMcpTestbed` | 테스트용 행 구조체, 위젯 부모 클래스, 게임 모드 |
| `Source/AgentMcpTestbedEditor` | `/Game/AgentMcpFixtures` 아래에 테스트 에셋을 만드는 `testbed_*` 도구, 롤백·취소 검사용 훅 |
| `Config` | 테스트베드는 포트 **18766**을 써서, 기본 포트를 쓰는 다른 프로젝트 대신 응답하는 일이 없음 |
| `Tools/mcp_smoke.py` | smoke 테스트(Python 3, 표준 라이브러리만 사용) |
| `Tools/mcp_call.py` | 명령줄에서 도구 하나 호출 |

1. `AgentMcpTestbedEditor` 타깃을 빌드합니다.
   `<UE>\Engine\Build\BatchFiles\Build.bat AgentMcpTestbedEditor Win64 Development -Project=<path>\AgentMcpTestbed.uproject -WaitMutex`
2. `AgentMcpTestbed.uproject`를 열고 `Agent MCP server listening on http://127.0.0.1:18766/mcp`가 나올 때까지 기다립니다.
3. `python Tools/mcp_smoke.py --out Saved/MCP/smoke.json`을 실행합니다.

smoke 테스트는 Play In Editor를 시작하고, 레벨을 바꾸고, 테스트 에셋을 저장합니다. 그래서 먼저 URL에 응답하는 에디터가
`AgentMcpTestbed` 프로젝트인지 확인하고, 아니면 멈춥니다. MCP 전송과 오류 처리, 모든 도구, undo와 롤백, 요청 취소,
Play In Editor, 게임 UI를 포함한 뷰포트 캡처, Live Coding을 검사합니다.

도구 하나만 호출하려면:

```
python Tools/mcp_call.py editor_get_state --url http://127.0.0.1:18766/mcp --expect-project AgentMcpTestbed
```

## 한계

- Windows 64비트의 Unreal Engine 5.5.4에서만 테스트했습니다. 모든 도구는 `Tools`의 Python 클라이언트로 테스트했습니다.
  Claude Code 2.1.270(데스크톱 앱과 CLI)에서는 `editor_get_state`, `actor_find`, `viewport_capture`(이미지 포함) 호출이
  성공하는 것을 확인했고, 나머지 도구는 아직 Claude Code에서 호출해 보지 않았습니다.
- 응답은 일반 JSON입니다. 스트리밍(SSE, 진행 알림)은 없습니다. `pie_start` 같은 도구는 끝날 때까지 요청을 붙잡고 있습니다.
- 요청은 에디터의 게임 스레드에서 처리됩니다. **Use Less CPU when in Background**가 켜진 채 에디터가 백그라운드에 있으면
  초당 약 3번만 틱하므로 호출마다 약 0.3초가 걸립니다. 에이전트가 작업하는 동안에는 이 에디터 설정을 끄세요.
- `livecoding_compile`은 컴파일이 끝날 때까지 에디터를 멈추고, Live Coding은 `UCLASS`, `USTRUCT`, `UPROPERTY`,
  `UFUNCTION` 선언 변경을 적용하지 못합니다. 이런 변경은 에디터를 닫고 빌드하세요.
- 에셋은 DataTable 행으로만 바꿀 수 있습니다. 블루프린트 그래프와 위젯 트리는 조회하고 컴파일할 수 있지만 편집할 수는
  없습니다.
- `ToolSearch` 노출 모드와 저장 시 소스 컨트롤 처리는 아직 테스트하지 않았습니다.

## 배경

도구 구성과 리플렉션 기반 설계는 Epic Games가 Unreal Engine 5.8에 포함한 실험적 Model Context Protocol·toolset plugin을
따르며, Unreal Engine 5.5용으로 새로 구현했습니다. 이 저장소에는 해당 plugin의 소스 파일이 들어 있지 않습니다. Unreal과
Unreal Engine은 Epic Games, Inc.의 상표 또는 등록 상표입니다.

## 라이선스

[MIT License](LICENSE).
