# 설정

[English](Settings.md)

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
| `BlockedProperties` | 비어 있음 | `object_set_properties`와 `umg` 도구가 거부할 `ClassName.PropertyName` 와일드카드 |
| `BusyWaitTimeoutSeconds` | `10` | 에디터가 저장, 가비지 수집, 에셋 로딩 중일 때 호출이 기다리는 시간 |
| `MaxResultBytes` | `65536` | 이 크기를 넘는 결과 텍스트는 잘림 |
| `LogBufferLines` | `20000` | `log_get_recent`용으로 보관하는 로그 줄 수 |
| `SkillDirectories` | 비어 있음 | 플러그인과 프로젝트 스킬 폴더 다음에 찾을 스킬 폴더. 상대 경로는 프로젝트 폴더 기준 |
| `SourceFolders` | `Source`, `Config`, `Plugins` | 소스 도구가 읽고 쓸 수 있는 폴더. 프로젝트 폴더 기준 |

AI 채팅은 사용자별 설정을 **에디터 환경설정 > 플러그인 > AI 채팅**에 따로 둡니다. [AI 채팅](AiChat.ko.md#설정)을 보세요.
