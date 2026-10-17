# MCP 아키텍처 — Unreal Editor와 AI 에이전트 연결

[English](Architecture.md) · [문서 목차](Index.ko.md) · [도구 작성](WritingTools.ko.md)

AgentMcp는 **Unreal Editor 프로세스 안에 MCP 서버를 실행**하고, C++ `UFUNCTION`을 MCP Tool로 노출합니다. 외부의 Claude Code·Codex뿐 아니라 에디터 내 AI 채팅도 같은 Tool을 호출합니다. 이 프로젝트의 핵심은 단순한 채팅 UI보다 **에디터 기능을 탐색·호출할 수 있게 만든 실행 계층**입니다.

UE 5.8의 실험적 MCP Toolset과 Agent Skill 개념을 참고해 **UE 5.5/5.7 환경에서 별도로 구현**했습니다. Epic 플러그인 코드를 그대로 이식하거나 UE 5.8과 동일한 API를 제공하는 것은 아닙니다.

## 전체 구조

```text
외부 AI 에이전트                     Unreal Editor의 AI 채팅 패널
Claude Code / Codex                  Claude Code / Codex / Gemini 등
        |                                         |
        | MCP 클라이언트                    AgentMcpChat
        |                                   (내부 MCP 클라이언트)
        +--------------------+--------------------+
                             |
                 127.0.0.1:<port>/mcp
                  Streamable HTTP + JSON
                             |
                      AgentMcpProtocol
                세션 / tools/list / tools/call
                             |
                       AgentMcpToolset
              Reflection 기반 Tool 등록·호출
               접근 수준 / 승인 / 트랜잭션
                             |
                        AgentMcpTools
             Actor / Asset / Blueprint / UMG
               PIE / Log / Viewport / Perf
                             |
                    Unreal Editor API
                      프로젝트 에셋·상태

        Agent Skills (SKILL.md)
        └─ skills_list / skills_get으로 별도 제공
           → 작업 규칙·순서 안내, 직접 에셋 수정은 하지 않음
```

AI 채팅은 모델 공급자 연결과 대화 UI를 담당하지만, Unreal 조작을 위한 별도 API를 만들지 않습니다. 패널의 `FMcpClient`가 **실행 중인 같은 에디터의 MCP 엔드포인트**에 `initialize` → `tools/list` → `tools/call` 순서로 연결합니다.

## 모듈별 책임

| 모듈 | 책임 | 주요 소스 |
|---|---|---|
| `AgentMcpProtocol` | HTTP 수신, MCP/JSON-RPC 메시지, 세션·요청·응답 | [`AgentMcpServer.cpp`](../Plugins/AgentMcp/Source/AgentMcpProtocol/Private/AgentMcpServer.cpp) |
| `AgentMcpToolset` | Tool 자동 검색·등록, Reflection 스키마/호출, 접근 제어, 실행 수명 | [`AgentMcpRuntime.cpp`](../Plugins/AgentMcp/Source/AgentMcpToolset/Private/AgentMcpRuntime.cpp), [`AgentMcpReflectedTool.cpp`](../Plugins/AgentMcp/Source/AgentMcpToolset/Private/AgentMcpReflectedTool.cpp) |
| `AgentMcpTools` | Unreal Editor API를 사용하는 실제 기능 | [`Toolsets/`](../Plugins/AgentMcp/Source/AgentMcpTools/Private/Toolsets) |
| `AgentMcpChat` | 에디터 패널, 모델 공급자 연결, MCP 클라이언트, 승인 UI | [`AgentMcpChatMcpClient.cpp`](../Plugins/AgentMcp/Source/AgentMcpChat/Private/AgentMcpChatMcpClient.cpp) |
| `AgentMcpCompat` | 버전별 호환 코드와 JSON 문자열·UTF-8 변환 | [`AgentMcpCompat.h`](../Plugins/AgentMcp/Source/AgentMcpCompat/Public/AgentMcpCompat.h) |

### 1. 서버 시작과 Tool 등록

에디터 초기화가 끝나면 `AgentMcpToolsetModule`이 `FAgentMcpRuntime::Startup`을 실행합니다. 런타임은 다음 방식으로 Tool을 구성합니다.

1. `GetDerivedClasses(UAgentMcpToolset::StaticClass(), …)`로 Toolset 클래스를 찾습니다.
2. 각 클래스에서 `AICallable` 메타데이터를 가진 `static UFUNCTION`을 선택합니다. `AIIgnore` 표시 함수는 제외합니다.
3. `FReflectedTool::Create`가 함수의 인자·반환 타입을 검사하고 입력 JSON Schema, 도구 이름·설명·접근 수준을 만듭니다.
4. `AllowedTools` / `BlockedTools` 설정으로 노출 대상을 걸러 서버에 등록합니다.

새 기능을 추가할 때 **서버의 등록 코드를 수정하지 않고** `UAgentMcpToolset` 하위 클래스에 함수를 선언할 수 있습니다. Toolset 이름과 함수 이름을 조합해 `umg_inspect`, `blueprint_compile` 같은 MCP 이름을 만듭니다.

```cpp
UCLASS(meta = (McpToolset = "my"))
class UMyTools : public UAgentMcpToolset
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,
        meta = (AICallable, McpAccess = "Read"))
    static FMyResult Inspect(const FString& AssetPath);
};
```

위 코드는 선언 형식을 보여 주는 예시입니다. `FMyResult` 정의와 구현은 생략했습니다. 세부 요구사항은 [도구 작성](WritingTools.ko.md)에 있습니다.

일반 등록 방식(`ExposureMode=Native`)은 Tool을 각각 노출합니다. `ToolSearch` 모드는 `toolsets_list`, `toolsets_describe`, `tools_call`만 노출하고 필요할 때 상세 스키마를 조회하도록 합니다. 두 모드 모두 내부적으로 같은 Tool 카탈로그를 사용합니다.

### 2. 요청 처리와 Unreal 함수 호출

실제 Tool 실행 경로는 다음과 같습니다.

```text
MCP tools/call(name, arguments)
  → FAgentMcpServerImpl::HandleToolsCall
  → 이름으로 Tool 검색 / 세션·취소 상태 연결
  → FReflectedTool::Run
  → 접근 수준·필요한 승인 확인
  → 에디터 상태 확인(PIE, 저장·GC·비동기 로드 등)
  → InvokeToolFunction
  → Reflection 인자 변환 / UFunction 실행 / 결과 JSON 변환
  → Tool 결과 또는 구조화된 오류 반환
```

`FReflectedTool`이 **Tool 메타데이터와 실행 정책**을 담당하고, [`AgentMcpInvoker.cpp`](../Plugins/AgentMcp/Source/AgentMcpToolset/Private/AgentMcpInvoker.cpp)가 입력값을 Unreal 타입으로 변환해 함수를 호출합니다. [`AgentMcpSchema.cpp`](../Plugins/AgentMcp/Source/AgentMcpToolset/Private/AgentMcpSchema.cpp)는 `UFunction`과 `UPROPERTY` 정보를 이용해 도구 입력 스키마를 만듭니다.

에디터 객체 작업은 **게임 스레드**에서 수행됩니다. 저장, GC, 비동기 로딩 때문에 에디터가 바쁘면 설정된 `BusyWaitTimeoutSeconds` 내에서 기다리거나 `EDITOR_BUSY` 오류를 반환합니다. PIE 시작·컴파일처럼 이후 프레임에서 완료되는 작업은 `UAgentMcpAsyncResult`와 Ticker를 통해 완료·취소·타임아웃을 처리할 수 있습니다. 이 비동기 결과 방식은 **Read/Control Tool에만 적용**됩니다. 여러 프레임에 걸쳐 에디터 Undo 트랜잭션을 유지할 수 없기 때문입니다.

### 3. MCP 전송과 세션

서버는 Unreal의 `HTTPServer`를 사용하며 기본 주소는 `http://127.0.0.1:18765/mcp`입니다. `POST`로 MCP JSON-RPC 요청을 처리하고, `DELETE`로 세션을 종료합니다. `initialize`에서 프로토콜 버전·서버 기능·작업 안내를 전달하며, `tools/list`는 스키마를, `tools/call`은 실행 결과를 반환합니다.

응답은 **일반 JSON**입니다. UE 5.5에서 사용한 HTTPServer 경로는 SSE 스트리밍을 제공하지 않으므로 `GET` 스트림과 진행 상황 전송을 지원하지 않습니다. 요청 취소와 세션 종료는 처리하지만, 서버가 외부 클라이언트에 임의의 새 작업을 요청하는 구조는 아닙니다.

## 변경 작업과 승인 책임

`FReflectedTool`은 `McpAccess`에 따라 Tool을 `Read`, `Write`, `Destructive`, `Control`로 구분합니다.

- **Read:** 에셋·상태 조회. 변경 승인 대상이 아닙니다.
- **Write / Destructive:** PIE 중 변경을 기본적으로 차단하고, 지원되는 Unreal 변경은 `FScopedTransaction`으로 기록합니다. 부분 변경이 발생한 뒤 호출이 실패하면 기록된 트랜잭션을 롤백합니다.
- **Control:** 에디터 열기·선택·PIE 등의 동작. 데이터 변경과 다른 정책을 적용합니다.
- **저장·복구:** 일반적인 에셋 변경은 자동 저장하지 않습니다. `asset_save`와 `level_save`가 분리되어 있으며, 파일 시스템의 `source_write` / `source_replace`는 `editor_undo`로 복구되지 않습니다.

**AI 채팅과 외부 MCP 연결의 승인 방식은 다릅니다.** 에디터 AI 채팅의 `FMcpClient`는 `X-AgentMcp-Client` 태그를 붙입니다. 태그가 붙은 연결의 비-Read 호출은 등록된 승인 게이트를 거쳐야 하므로 채팅 패널에 승인/거부 카드가 표시됩니다. **태그 없이 직접 연결한 Claude Code·Codex에는 이 승인 카드가 자동 적용되지 않습니다.** 프로젝트 단위 제한에는 `AllowedTools`, `BlockedTools`, `BlockedProperties`를 사용합니다.

[권한·제한 상세](Safety.ko.md) · [AI 채팅 공급자와 권한 모드](AiChat.ko.md)

## Agent Skills는 별도의 지침 계층

Tool 등록과 Skill 로딩은 독립적입니다. `AgentMcpRuntime`은 서버 초기 안내에 현재 Skill 이름·설명을 포함하고, 에이전트는 `skills_list` / `skills_get`으로 지침을 요청합니다.

`skills_get`은 실제 `SKILL.md`를 호출 시점에 다시 읽고, 프로젝트 폴더의 같은 이름 Skill이 플러그인 기본 지침을 대체할 수 있습니다. **Skill은 Tool을 직접 실행하는 엔진이 아니라, 에이전트가 Tool을 사용하는 방법을 제공하는 파일**입니다.

[동적 Skill 로딩·오버라이드 구조](Skills.ko.md)

## 구현 선택과 범위

- **Unreal Reflection 활용:** C++ 함수와 메타데이터에서 Tool 스키마를 만들어 Tool 추가 시 수동 등록과 별도 API 정의를 줄였습니다. 대신 지원 가능한 인자 타입·리플렉션 규칙에 제약이 있습니다.
- **에디터 내 로컬 서버:** 프로젝트 에셋과 Slate/UMG·Blueprint·PIE 상태에 직접 접근할 수 있지만, 에디터 실행 상태와 게임 스레드의 영향을 받습니다.
- **하나의 Tool 계층, 여러 클라이언트:** 외부 MCP 클라이언트와 내장 채팅이 동일한 기능을 사용합니다. 다만 연결 방식과 승인 정책까지 동일한 것은 아닙니다.
- **Tool과 Skill 분리:** 도구는 조작을 수행하고, Markdown Skill은 프로젝트별 제작 규칙과 조사 순서를 전달합니다.

[클라이언트 연결](Clients.ko.md) · [전체 Tool 목록](Tools.ko.md) · [UMG 제작 사례](Samples/DungeonUi.ko.md)
