---
name: umg-authoring
description: Build or restyle Unreal Engine UMG user interfaces (HUDs, menus, popups, lists, result screens) through the Agent MCP umg tools the way a production UI team works - reusable component Widget Blueprints for repeated elements, one place for style values, C++ BindWidget contracts, data-driven lists and a capture-based review. Use whenever a task calls umg_create_widget_blueprint, umg_add_widgets, umg_set_widget_properties or umg_remove_widgets, or asks for game UI in an Unreal project that has the Agent MCP server.
---

# Production UMG with Agent MCP

The umg tools can build any widget tree, including a flat copy-paste one. This skill is about building the tree that a UI team
would keep: small reusable pieces, data instead of duplicated text, and style values that change in one place.

## 1. Plan before the first tool call

Write a short plan first and keep it in the conversation:

- **Screen tree.** Sketch the screen as nested boxes.
- **Components.** Mark every element that appears more than once on the screen (list rows, reward or inventory slots, stat tiles,
  tabs, buttons, badges) or that other screens will need. Each becomes its own Widget Blueprint. Screens arrange components; they
  do not repeat their insides.
- **Data.** For each component, list its inputs (name, count, rarity, progress, state). Inputs are C++ properties, not text typed
  into several copies of a subtree.
- **Dynamic lists.** When the number of entries comes from data, the screen holds a `DynamicEntryBox` (small lists) or a
  `ListView`/`TileView` (long, scrolling lists) whose entry class is the component; code creates the entries.
- **Style tokens.** Name the few colors, radii and font sizes the screens use (panel, accent, text, muted text, success, danger,
  rarity colors) and keep them in one theme data asset that the widgets read, so that a style change needs no build.
- **Content data.** Values that differ per item (names, icons, rarity) live in a DataTable; components reference a row instead of
  copying its values.
- **Decoration.** Panels, section dividers, corner ornaments and title plates come from the project's UI kit in
  `Art/Style/ui_style.md` (see the `ui-art-requests` skill), not from decoration made for one screen. Lines, dividers, outlines and
  small diamonds are brushes with theme colors; only icons, emblems and ornate pieces need textures.
- **Art.** Images the screen needs but the project does not have yet (item icons, emblems, kit textures) go into an art request; see
  the `ui-art-requests` skill. Until they arrive, components draw a fallback such as a brush shape.
- **Motion.** Decide for each animated element whether it is simple motion in code or a timeline animation (section 4).
- **What already exists.** Look before creating: `asset_find` for Widget Blueprints and style assets, `class_find_derived` on
  `UserWidget` for C++ bases, `umg_inspect` on similar screens. Follow the project's conventions (folders, prefixes, CommonUI)
  when it has them.

## 2. Components

- **C++ base per component** (a `UUserWidget` subclass, `UCLASS(Abstract)`):
  - `BindWidget` for every widget the code touches; the names are a contract, so keep them stable.
  - `UPROPERTY(EditAnywhere, BlueprintReadWrite)` for the inputs, often one struct such as `Reward`.
  - Apply the inputs in `NativePreConstruct`, so that the component's own designer, parent screens and instances all preview them.
  - A setter (`SetReward`, `SetProgress`) for runtime updates.
  - Colors and frames that depend on data (rarity, state) come from the theme data asset; shapes, paddings and static colors stay
    in the Widget Blueprint so that designers can still change them.
  - Textures are soft references in the data, loaded when the component applies its inputs. While one is missing, the component
    draws its fallback, so the screen works before the art exists.
- **Layers for frames.** A `Border` whose brush is a texture draws only that texture, so a frame image would also remove the fill of
  that brush. Build a framed element as an `Overlay`: a fill `Border` that holds the content, and on top a frame `Border`
  (`HitTestInvisible`) whose brush the code or the theme sets. The border of a frame image ends inside the fill's padding.
- **Build the component once:** `umg_create_widget_blueprint` with `parentClass`, `umg_add_widgets` for its tree, then
  `blueprint_compile`. Fix every missing `BindWidget` before moving on.
- **Place instances** in screens with an entry whose class is the component's generated class, and set its inputs as instance
  properties:
  `{"class": "/Game/UI/Components/WBP_StatTile.WBP_StatTile_C", "name": "TimeTile", "properties": {"Label": "Clear time"}}`
- **Entry classes of lists** are widget properties, so the tools can set them:
  `{"class": "DynamicEntryBox", "name": "RewardList", "properties": {"EntryWidgetClass": "/Game/UI/Components/WBP_RewardSlot.WBP_RewardSlot_C", "EntryBoxType": "Horizontal", "EntrySpacing": {"X": 10, "Y": 0}}}`
- Blueprint graphs cannot be edited through the tools. Behavior belongs in C++ bases, or tell the user what to add in the graph.

## 3. Screens

- A screen's C++ base binds components by their C++ class (`TObjectPtr<UMyStatTile> TimeTile`) and passes data into them; it
  does not reach into the components' child widgets.
- Keep demo or preview data out of shipping screens: put it on instances in a separate demo or gallery Widget Blueprint, or in data
  assets.
- Set `isVariable` only for widgets that graphs use; `BindWidget` works without it.
- Use names that say the role (`RewardList`, `ClearTimeText`), `WBP_` for Widget Blueprints, and one folder per feature with a
  `Components` subfolder.

## 4. Motion

- **Simple motion that follows data** (fades, slides, pops, count-ups, entries that appear one after another) is C++ in the widget's
  base: set render opacity, render transforms and texts from the elapsed time in `NativeTick`. Keep durations, delays, distances and
  scales in `UPROPERTY(EditAnywhere)` values rather than constants, so that they can be tuned without a build.
- **Motion that someone shapes on a timeline** is a UMG widget animation made in the Widget Blueprint editor. The C++ base binds it
  with `UPROPERTY(Transient, meta = (BindWidgetAnim)) TObjectPtr<UWidgetAnimation> Intro;` and plays it. `umg_inspect` lists the
  animations of a Widget Blueprint.
- The tools cannot create or edit widget animations. When a screen needs timeline animation, describe it for the person who makes it
  (widgets, properties, timing, easing) instead of imitating it in code.

## 5. Tool mechanics

- `umg_add_widgets` checks every entry before it changes anything and reports all problems at once. Send a whole subtree in one
  call, then fix the reported entries and send it again.
- Property names are the C++ names (`Text`, `Font`, `Padding`, `LayoutData`, `WidgetStyle`). A struct value may list only the
  fields to set. Enum values are names (`HAlign_Center`, `RoundedBox`, `Fill`). Results use camelCase for struct fields and read
  back only the requested fields; `umg_inspect` with `bIncludeProperties` shows complete values.
- `object_list_properties` on a class default object (`/Script/UMG.Default__TextBlock`, `/Script/UMG.Default__CanvasPanelSlot`)
  lists property names and types.
- A `Border` holds one child; use a `SizeBox` for fixed sizes; `RoundedBox` brushes need no texture.
- A 9-slice brush (`DrawAs` `Box`) draws its margins at the texture's pixel size, whatever `ImageSize` says: size the texture for
  the border that should appear.
- The tools cannot move a widget to another parent. To change the nesting, remove the subtree with `umg_remove_widgets` and add it
  again with the same names, so that the `BindWidget` contracts stay bound.
- `ProgressBar` multiplies its fill by `FillColorAndOpacity`, which defaults to blue; set it to white when the brush carries the
  color.
- Writes are refused during Play In Editor; call `pie_stop` before editing. Nothing is saved until `asset_save`.

## 6. Review like a UI lead

1. `blueprint_compile` every changed Widget Blueprint, components first.
2. Capture before and after. Before a change to layout, style or art, look at the screen in a play session: `pie_start`, show the
   screen the way the project does (the Agent MCP testbed has `sample_show_widget`), `viewport_capture`, `pie_stop`. Capture it
   again after the change at the same viewport size, and compare the two with `scripts/capture_compare.py` of the `ui-style-system`
   skill, which reports how much changed and marks the changed pixels. Wait for intro animations to finish before
   capturing; when timing is the point of a change, also capture at the moment that matters.
3. Check the captures critically: content stays inside its panels and frames, nothing moved that should not, text fits and is not
   clipped, alignment and spacing follow one grid, colors match the tokens, decoration follows the UI kit, CJK glyphs render,
   nothing overlaps at the play viewport size, repeated elements look identical.
4. Fix with `umg_set_widget_properties` on the component when the problem repeats, not on each instance.
5. `asset_save` components and screens at the end.

## 7. Report

Tell the user which components were created and where they are used, the inputs of each, the style tokens, what the captures
showed, and anything left for a designer or for Blueprint graphs.
