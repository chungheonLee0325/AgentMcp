# Tools

[한국어](Tools.ko.md)

**Read** tools have no side effects. **Write** tools run in an undoable editor transaction and are refused during Play In Editor. **Destructive** tools are Write tools that only report what they would do unless `bConfirm` is true. **Control** tools change editor state that cannot be undone: play sessions, compiling, saving, and creating or importing assets.

| Tool | Access | Description |
|---|---|---|
| `editor_get_state` | Read | Open level, play session, unsaved packages, selection, undo history and server state |
| `editor_undo`, `editor_redo` | Control | Undo or redo the last editor transaction |
| `editor_write_client_config` | Control | Write the client configuration that points an agent at this editor, optionally including the user's Codex configuration |
| `editor_open_assets` | Control | Open assets in their editors for the person to look at; levels are left to `level_open` |
| `editor_show_in_content_browser` | Control | Show and select assets in the Content Browser without loading them |
| `log_get_recent` | Read | Editor log lines after a sequence number, filtered by verbosity, category or text |
| `actor_find` | Read | Actors by label or name, class, tag, folder or selection, in the editor or play world |
| `actor_inspect` | Read | Class, Blueprint, tags, transform, attachment and components of an actor |
| `actor_select` | Control | Select actors of the editor level and frame them in the viewport |
| `actor_set_transform` | Write | Move, rotate or scale an actor |
| `actor_spawn` | Write | Place actors from mesh or Blueprint assets, or from native classes, with label, transform, folder and properties |
| `actor_duplicate` | Write | Copy an actor in a row with a fixed offset, or onto given transforms |
| `actor_attach` | Write | Attach one actor to another, optionally on a socket |
| `actor_set_folder` | Write | Move actors to a World Outliner folder |
| `actor_delete` | Destructive | Delete actors together with the actors attached to them |
| `level_new` | Control | Create a level, empty or from a template, save it and open it |
| `level_open` | Control | Open a level in the editor |
| `level_save` | Control | Save the level open in the editor, or every level with unsaved changes |
| `object_list_properties` | Read | Properties of an object and whether they can be changed |
| `object_get_properties` | Read | Property values as JSON |
| `object_set_properties` | Write | Change properties of an actor or component in the editor level, of a project asset such as a data asset or texture, or the class defaults of a project Blueprint, with readback |
| `pie_start`, `pie_stop` | Control | Start or stop Play In Editor and wait until the session has begun or shut down. `pie_start` plays in the level viewport, or with `windowWidth` and `windowHeight` in a new window whose viewport has that size |
| `pie_status` | Read | Whether a play session is starting or running |
| `asset_find` | Read | Assets by folder, class and name, without loading them |
| `asset_inspect` | Read | Class, tags, file size, loaded and dirty state and reference counts of an asset |
| `asset_referencers`, `asset_dependencies` | Read | Packages that reference an asset, or that it depends on |
| `asset_save` | Control | Save loaded project assets without dialogs |
| `asset_create` | Control | Create a data asset, a DataTable or a string table |
| `asset_import_textures` | Control | Import PNG, JPEG, TGA or BMP files as textures, with the settings for UMG. `bMipmaps` keeps mipmaps for images drawn smaller than their size or at several sizes |
| `asset_import_meshes` | Control | Import FBX, glTF, GLB or OBJ files as static meshes, with their size and collision |
| `class_find_derived` | Read | C++ and Blueprint classes that derive from a class, with headers and assets |
| `datatable_get_schema` | Read | Row struct, C++ header, columns, C++ types and JSON schemas |
| `datatable_list_rows`, `datatable_get_rows` | Read | Row names, and values keyed by row and column |
| `datatable_set_rows`, `datatable_add_rows`, `datatable_rename_rows` | Write | Change, add or rename rows of a project DataTable |
| `datatable_remove_rows` | Destructive | Remove rows of a project DataTable |
| `stringtable_get_entries` | Read | Namespace, keys and source strings of a string table |
| `stringtable_set_entries` | Write | Add entries to a project string table or change their strings, and set its namespace |
| `blueprint_inspect` | Read | Parent chain, interfaces, components, variables, functions and graphs |
| `blueprint_compile` | Control | Compile a Blueprint or Widget Blueprint and return its errors and warnings |
| `anim_build_blend_space` | Control | Create or rebuild a 1D blend space from animations placed on one axis, with optional smoothing of the axis |
| `anim_build_montage` | Control | Create or rebuild a montage whose segments start named sections, with loops and jumps between sections |
| `anim_build_anim_blueprint` | Control | Create an Animation Blueprint, or rebuild its anim graph, from a tree of inertialization, slot, blend space and sequence nodes, and compile it |
| `umg_inspect` | Read | Widget tree with slots, BindWidget properties, animations and property bindings |
| `umg_create_widget_blueprint` | Control | Create a Widget Blueprint with a parent class and a root panel |
| `umg_add_widgets` | Write | Add widgets, whole subtrees or Widget Blueprint instances, with widget and slot properties, in one call |
| `umg_set_widget_properties` | Write | Change widget properties, slot values and the variable flag of several widgets |
| `umg_remove_widgets` | Destructive | Remove widgets with their descendants, property bindings and graph references |
| `viewport_capture` | Read | PNG of the level viewport or the play session, including the game UI |
| `viewport_set_camera` | Control | Point the level viewport camera at a place or frame an actor, so the next capture shows it |
| `livecoding_compile` | Control | Compile changed C++ with Live Coding and wait for the result |
| `skills_list` | Read | Skills of the plugin and the project with their descriptions, and skill files that were skipped |
| `skills_get` | Read | Instructions of a skill, or one of its other files |
| `source_find` | Read | Text files of the readable source folders by name or relative path |
| `source_search` | Read | Lines of those files that contain a text or match a regular expression |
| `source_read` | Read | Numbered lines of a file, decoding UTF-8, UTF-16 and CP949 |
| `source_replace` | Write | Replace exact text in a file, keeping its encoding and line endings |
| `source_write` | Write | Create a text file, or replace one with `bOverwrite`, keeping the encoding and line endings of an existing file |

A typical verification loop: `blueprint_compile` → `pie_start` → `log_get_recent` from the returned `startLogSequence` → `viewport_capture` → `pie_stop`.

To build a user interface: `umg_create_widget_blueprint` (with a C++ parent class that declares `BindWidget` properties) → `umg_add_widgets` → `blueprint_compile` → inspect it in a play session with `viewport_capture` → adjust it with `umg_set_widget_properties`. Values are JSON: property names are the C++ names (`Text`, `Font`, `Padding`, `LayoutData`), struct values may list only the fields to set, and enum values are names (`HAlign_Center`, `RoundedBox`). Widget Blueprint classes can be nested as component instances and their instance properties can be set the same way.

To keep presentation in data, `asset_create` can make a theme data asset or item DataTable, `object_set_properties` and the datatable tools fill them, and `asset_import_textures` brings in icons and frames with UMG texture settings. Visible text goes into a string table (`asset_create` with `StringTable`, then `stringtable_set_entries`), and a text property references an entry when it is set to `LOCTABLE("/Game/Text/ST_Ui.ST_Ui", "Key")`, so the text keeps one key for localization however often a script writes it.

To block out a level: `level_new` → `actor_spawn` with meshes or Blueprints → `actor_duplicate` for rows and grids → `actor_attach` and `actor_set_folder` to keep the outliner readable → `viewport_set_camera` and `viewport_capture` to look at it → `level_save`. `actor_set_transform` and `object_set_properties` adjust what is already placed, and `editor_undo` reverts the last call.

To animate a character: `anim_build_blend_space` for locomotion → `anim_build_montage` for each attack, with a looping section where the game decides when to go on → `anim_build_anim_blueprint` with a Slot node over the blend space that reads the speed from a variable of the C++ parent class → `asset_save`.

To read or change code: `source_find` or `source_search` → `source_read` → `source_replace` → `livecoding_compile`. The source tools read only the folders in `SourceFolders` (`Source`, `Config` and `Plugins` by default) and skip `Binaries`, `Intermediate` and `Saved`. A file saved in CP949, as Korean projects often are, is written back in CP949. Source edits are no editor transactions, so `editor_undo` does not revert them; use source control.

To show something to the person: `editor_open_assets`, `editor_show_in_content_browser`, `actor_select` and `viewport_set_camera` change no project data, so the [AI chat](AiChat.md) runs them without asking in every permission mode.
