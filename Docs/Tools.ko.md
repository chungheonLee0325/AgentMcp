# 도구

[English](Tools.md)

**Read** 도구는 부작용이 없습니다. **Write** 도구는 되돌릴 수 있는 에디터 트랜잭션 안에서 실행되고, Play In Editor 중에는 거부됩니다. **Destructive** 도구는 `bConfirm`이 true가 아니면 무엇을 할지 보고만 하는 Write 도구입니다. **Control** 도구는 플레이 세션, 컴파일, 저장, 에셋 생성과 가져오기처럼 되돌릴 수 없는 에디터 상태를 바꿉니다.

| 도구 | 접근 | 설명 |
|---|---|---|
| `editor_get_state` | Read | 열린 레벨, 플레이 세션, 저장 안 된 패키지, 선택, undo 기록, 서버 상태 |
| `editor_undo`, `editor_redo` | Control | 마지막 에디터 트랜잭션 되돌리기와 다시 실행 |
| `editor_write_client_config` | Control | 이 에디터를 가리키는 클라이언트 설정 파일 작성. 사용자 Codex 설정까지 선택 가능 |
| `editor_open_assets` | Control | 사람이 볼 수 있도록 에셋을 에디터로 연다. 레벨은 `level_open`에 맡김 |
| `editor_show_in_content_browser` | Control | 에셋을 로드하지 않고 콘텐츠 브라우저에서 보여 주고 선택 |
| `log_get_recent` | Read | 시퀀스 번호 이후의 에디터 로그. 로그 수준, 카테고리, 텍스트로 거르기 |
| `actor_find` | Read | 레이블이나 이름, 클래스, 태그, 폴더, 선택으로 액터 찾기(에디터 또는 플레이 월드) |
| `actor_inspect` | Read | 액터의 클래스, 블루프린트, 태그, 트랜스폼, 부착 관계, 컴포넌트 |
| `actor_select` | Control | 에디터 레벨의 액터를 선택하고 뷰포트에 비춤 |
| `actor_set_transform` | Write | 액터 이동, 회전, 크기 조절 |
| `actor_spawn` | Write | 메시·블루프린트 에셋이나 네이티브 클래스로 액터를 배치. 레이블, 트랜스폼, 폴더, 프로퍼티 지정 |
| `actor_duplicate` | Write | 고정 간격으로 줄지어 복제하거나, 주어진 트랜스폼마다 복제 |
| `actor_attach` | Write | 액터를 다른 액터에, 필요하면 소켓에 부착 |
| `actor_set_folder` | Write | 액터를 World Outliner 폴더로 이동 |
| `actor_delete` | Destructive | 액터와 거기 붙어 있는 액터를 함께 삭제 |
| `level_new` | Control | 빈 레벨이나 템플릿으로 레벨을 만들어 저장하고 연다 |
| `level_open` | Control | 에디터에서 레벨을 연다 |
| `level_save` | Control | 에디터에 열린 레벨, 또는 저장 안 된 모든 레벨을 저장 |
| `object_list_properties` | Read | 객체의 프로퍼티와 변경 가능 여부 |
| `object_get_properties` | Read | 프로퍼티 값을 JSON으로 읽기 |
| `object_set_properties` | Write | 에디터 레벨의 액터·컴포넌트, 데이터 에셋·텍스처 같은 프로젝트 에셋, 프로젝트 블루프린트의 클래스 기본값과 컴포넌트 기본값의 프로퍼티 변경과 변경 후 값 확인 |
| `pie_start`, `pie_stop` | Control | Play In Editor를 시작하거나 종료하고, 세션이 시작되거나 끝날 때까지 대기. `pie_start`는 레벨 뷰포트에서 실행하고, `windowWidth`와 `windowHeight`를 주면 뷰포트가 그 크기인 새 창에서 실행 |
| `pie_status` | Read | 플레이 세션이 시작 중인지, 실행 중인지 |
| `asset_find` | Read | 폴더, 클래스, 이름으로 에셋 찾기(에셋을 로드하지 않음) |
| `asset_inspect` | Read | 에셋의 클래스, 태그, 파일 크기, 로드·변경 상태, 참조 수 |
| `asset_referencers`, `asset_dependencies` | Read | 에셋을 참조하는 패키지, 에셋이 의존하는 패키지 |
| `asset_save` | Control | 로드된 프로젝트 에셋을 대화상자 없이 저장 |
| `asset_create` | Control | 데이터 에셋, DataTable, 스트링 테이블, 부모 클래스를 지정한 블루프린트 클래스 생성 |
| `asset_import_textures` | Control | PNG, JPEG, TGA, BMP 파일을 UMG용 설정의 텍스처로 가져오기. `bMipmaps`를 주면 제 크기보다 작게 또는 여러 크기로 그리는 이미지를 위해 밉맵을 유지 |
| `asset_import_meshes` | Control | FBX, glTF, GLB, OBJ 파일을 스태틱 메시로 임포트. 크기와 콜리전을 함께 보고 |
| `class_find_derived` | Read | 클래스를 상속하는 C++·블루프린트 클래스와 헤더, 에셋 |
| `datatable_get_schema` | Read | 행 구조체, C++ 헤더, 열, C++ 타입, JSON 스키마 |
| `datatable_list_rows`, `datatable_get_rows` | Read | 행 이름, 행과 열로 정리한 값 |
| `datatable_set_rows`, `datatable_add_rows`, `datatable_rename_rows` | Write | 프로젝트 DataTable 행 변경, 추가, 이름 변경 |
| `datatable_remove_rows` | Destructive | 프로젝트 DataTable 행 삭제 |
| `stringtable_get_entries` | Read | 스트링 테이블의 네임스페이스, 키, 원문 |
| `stringtable_set_entries` | Write | 프로젝트 스트링 테이블 항목 추가나 원문 변경, 네임스페이스 설정 |
| `blueprint_inspect` | Read | 부모 클래스 체인, 인터페이스, 컴포넌트, 변수, 함수, 그래프 |
| `blueprint_compile` | Control | 블루프린트나 위젯 블루프린트를 컴파일하고 오류와 경고 반환 |
| `blueprint_get_graph` | Read | 그래프의 노드와 핀, 값, 연결. 진입점만 또는 한 노드에 이어진 체인만 볼 수도 있음 |
| `blueprint_read_graph` | Read | 실행 연결을 따라가는 짧은 의사코드로 그래프 읽기. 이벤트, 함수, 분기, 루프, 식 |
| `blueprint_find_usages` | Read | Find in Blueprints 인덱스로 함수 호출, 변수 읽기·쓰기, 캐스트, 이벤트 바인딩 위치 찾기. 로드하지 않은 블루프린트도 포함 |
| `blueprint_find_node_types` | Read | 그래프에 넣을 수 있는 노드 타입을 에디터 노드 메뉴처럼 `Development\|PrintString` 같은 타입 ID로 반환. 특정 핀에 연결되는 것만 고를 수도 있음 |
| `blueprint_edit_graph` | Write | 노드 추가·삭제·이동, 핀 연결·끊기, 입력값 설정, 핀 추가를 순서대로, undo 한 단계로 수행 |
| `blueprint_add_members` | Write | 블루프린트에 변수, 입출력이 있는 함수, 이벤트 디스패처를 undo 한 단계로 추가 |
| `anim_build_blend_space` | Control | 한 축에 애니메이션을 배치한 1D 블렌드 스페이스를 만들거나 다시 구성, 축 입력 스무딩 선택 |
| `anim_build_montage` | Control | 세그먼트마다 이름 붙은 섹션을 두고 섹션 반복과 이동을 지정한 몽타주를 만들거나 다시 구성 |
| `anim_build_anim_blueprint` | Control | 관성 블렌드(Inertialization), 슬롯, 블렌드 스페이스, 시퀀스 노드 트리로 애니메이션 블루프린트를 만들거나 애님 그래프를 다시 구성하고 컴파일 |
| `umg_inspect` | Read | 슬롯을 포함한 위젯 트리, BindWidget 프로퍼티, 애니메이션, 프로퍼티 바인딩 |
| `umg_create_widget_blueprint` | Control | 부모 클래스와 루트 패널을 지정해 위젯 블루프린트 생성 |
| `umg_add_widgets` | Write | 위젯, 하위 트리 전체, 위젯 블루프린트 인스턴스를 위젯·슬롯 프로퍼티와 함께 한 번에 추가 |
| `umg_set_widget_properties` | Write | 여러 위젯의 프로퍼티, 슬롯 값, 변수 여부 변경 |
| `umg_remove_widgets` | Destructive | 위젯과 하위 위젯, 프로퍼티 바인딩, 그래프 참조 삭제 |
| `viewport_capture` | Read | 레벨 뷰포트나 플레이 세션의 PNG 캡처(게임 UI 포함) |
| `viewport_set_camera` | Control | 레벨 뷰포트 카메라를 특정 위치로 옮기거나 액터를 비춰, 다음 캡처에 나오게 함 |
| `livecoding_compile` | Control | 바뀐 C++를 Live Coding으로 컴파일하고 결과까지 대기 |
| `perf_summarize_csv` | Read | 실행 묶음별 CSV 프로파일러 캡처: 열마다 중앙값·편차·95번째 백분위, 표시한 이벤트마다 비용 |
| `perf_compare_csv` | Read | 두 캡처 폴더 비교: 열과 이벤트마다 변화, 양쪽 편차보다 크면 표시 |
| `perf_read_gpu_profile` | Read | 로그 파일의 `ProfileGPU` 덤프를 패스 트리로 |
| `perf_trace_hotspots` | Control | UnrealInsights.exe로 Unreal Insights 트레이스를 내보내고, 표시한 순간의 프레임마다 타이머별 자체·포함 시간을 합산 |
| `skills_list` | Read | 플러그인과 프로젝트의 스킬과 설명, 건너뛴 스킬 파일 |
| `skills_get` | Read | 스킬의 지침이나 스킬에 딸린 파일 하나 |
| `source_find` | Read | 읽을 수 있는 소스 폴더의 텍스트 파일을 이름이나 상대 경로로 찾음 |
| `source_search` | Read | 그 파일들에서 텍스트를 포함하거나 정규식에 맞는 줄 |
| `source_read` | Read | 파일의 줄을 번호와 함께 읽음. UTF-8, UTF-16, CP949 해석 |
| `source_replace` | Write | 파일의 정확한 텍스트를 바꿈. 인코딩과 줄바꿈 유지 |
| `source_write` | Write | 텍스트 파일을 만들거나 `bOverwrite`로 통째로 바꿈. 기존 파일의 인코딩과 줄바꿈 유지 |

일반적인 검증 흐름: `blueprint_compile` → `pie_start` → 반환된 `startLogSequence`부터 `log_get_recent` → `viewport_capture` → `pie_stop`

UI를 만드는 흐름: `umg_create_widget_blueprint`(`BindWidget` 프로퍼티를 선언한 C++ 부모 클래스 지정) → `umg_add_widgets` → `blueprint_compile` → 플레이 세션에서 `viewport_capture`로 확인 → `umg_set_widget_properties`로 조정. 값은 JSON입니다. 프로퍼티 이름은 C++ 이름(`Text`, `Font`, `Padding`, `LayoutData`)을 쓰고, 구조체 값에는 바꿀 필드만 적어도 되며, enum 값은 이름(`HAlign_Center`, `RoundedBox`)으로 씁니다. 엔트리 클래스로 위젯 블루프린트를 쓸 수 있고, 그 인스턴스 속성도 같은 방식으로 설정합니다. 편집 도구는 요청한 필드만 되읽어 돌려주고, 전체 값은 `umg_inspect`의 `bIncludeProperties`로 확인합니다.

UI의 모양을 데이터로 두려면 `asset_create`로 테마 데이터 에셋이나 아이템 DataTable을 만들고, `object_set_properties`와 datatable 도구로 채우고, `asset_import_textures`로 아이콘과 프레임을 UMG용 텍스처 설정으로 가져옵니다. 화면에 보이는 글은 스트링 테이블에 둡니다(`asset_create`의 `StringTable`, 이어서 `stringtable_set_entries`). 글 속성을 `LOCTABLE("/Game/Text/ST_Ui.ST_Ui", "Key")`로 설정하면 그 항목을 참조하므로, 스크립트가 몇 번을 다시 써도 로컬라이제이션 키가 하나로 유지됩니다.

블루프린트 로직 수정 흐름: 새 클래스는 `asset_create`(`assetClass` `Blueprint`) → `blueprint_add_members`로 변수와 함수 추가 → `blueprint_get_graph`로 그래프 읽기 → 노드 타입마다 `blueprint_find_node_types` → `blueprint_edit_graph`로 노드를 추가하고 반환된 핀 이름으로 연결 → `blueprint_compile` → `pie_start`와 `log_get_recent`로 실행 확인 → `asset_save`

성능 수정을 찾고 증명하는 흐름: 에디터를 닫고 `perf-investigation` 스킬의 `run.py`로 프로젝트의 성능 장면 실행 → `Saved/Perf/<label>/csv`에 `perf_summarize_csv` → 끊김의 원인은 `run.py --trace`와 `perf_trace_hotspots` → 수정 → 새 라벨로 같은 실행 → `perf_compare_csv`

캐릭터 애니메이션 흐름: 이동은 `anim_build_blend_space` → 공격마다 `anim_build_montage`(게임이 다음 단계를 정할 때까지 반복하는 섹션 포함) → `anim_build_anim_blueprint`로 블렌드 스페이스 위에 Slot 노드를 두고 속도는 C++ 부모 클래스 변수에서 읽음 → `asset_save`

코드를 읽거나 고치는 흐름: `source_find` 또는 `source_search` → `source_read` → `source_replace` → `livecoding_compile`. 소스 도구는 `SourceFolders`에 있는 폴더(기본값 `Source`, `Config`, `Plugins`)만 읽고 `Binaries`, `Intermediate`, `Saved`는 건너뜁니다. 한국 프로젝트에 흔한 CP949 파일은 CP949로 다시 씁니다. 소스 편집은 에디터 트랜잭션이 아니어서 `editor_undo`로 되돌릴 수 없으니 소스 컨트롤을 쓰세요.

사람에게 무언가를 보여 주는 흐름: `editor_open_assets`, `editor_show_in_content_browser`, `actor_select`, `viewport_set_camera`는 프로젝트 데이터를 바꾸지 않으므로 [AI 채팅](AiChat.ko.md)은 어떤 권한 모드에서든 묻지 않고 실행합니다.
