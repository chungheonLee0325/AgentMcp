# Settings

[한국어](Settings.ko.md)

Set values in `Config/DefaultEditorPerProjectUserSettings.ini` of the project:

```ini
[/Script/AgentMcpToolset.AgentMcpSettings]
Port=18765
+BlockedTools=datatable_remove_rows
```

| Key | Default | Meaning |
|---|---|---|
| `bAutoStartServer` | `True` | Start the server when the editor has loaded |
| `Port` | `18765` | Loopback port |
| `UrlPath` | `/mcp` | Endpoint path |
| `AuthToken` | empty | When set, clients must send `Authorization: Bearer <token>` |
| `ExposureMode` | `Native` | `Native` registers every tool. `ToolSearch` registers only `toolsets_list`, `toolsets_describe` and `tools_call` |
| `BlockedTools`, `AllowedTools` | empty | Tool-name wildcards such as `datatable_*` |
| `bAllowWritesDuringPIE` | `False` | Allow Write tools during Play In Editor |
| `BlockedProperties` | empty | `ClassName.PropertyName` wildcards refused by `object_set_properties` and UMG tools |
| `BusyWaitTimeoutSeconds` | `10` | How long a call waits while the editor saves, collects garbage or loads assets |
| `MaxResultBytes` | `65536` | Result text above this size is truncated |
| `LogBufferLines` | `20000` | Log lines kept for `log_get_recent` |
| `SkillDirectories` | empty | Additional skill folders searched after the plugin and project folders |
| `SourceFolders` | `Source`, `Config`, `Plugins` | Folders, relative to the project, that the source tools may read and write |

The AI chat keeps its own per-user settings in **Editor Preferences > Plugins > AI 채팅**; see [AI chat](AiChat.md#settings).
