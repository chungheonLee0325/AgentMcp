# Dungeon UI sample

[한국어](DungeonUi.ko.md)

A dungeon progress HUD and a dungeon result popup in the style of a creature-collecting survival game. Claude Code (desktop app,
version 2.1.270) built them in the Agent MCP testbed through the tools; nobody opened the UMG designer. This page records how,
including the versions that reviews sent back, so that the workflow can be judged and not only the screenshots.

![Result popup over the HUD](../Images/dungeon_result.jpg)

![Dungeon progress HUD](../Images/dungeon_hud.jpg)

## Contents

| Asset under `/Game/Samples/DungeonUi` | C++ class | Role |
|---|---|---|
| `Components/WBP_RewardSlot` | `AgentMcpSampleRewardSlot` | Reward slot with icon, name and count. Its `Reward` input is a row of the item table and a count; the theme gives the rarity colors and frames. |
| `Components/WBP_StatTile` | `AgentMcpSampleStatTile` | Label above a large value: `Label`, `Value`, `SetValue`. |
| `Components/WBP_ObjectiveRow` | `AgentMcpSampleObjectiveRow` | Check mark, label and done/total. Open objectives use the theme accent unless the instance sets `AccentColor`; completed ones use the success color. |
| `WBP_DungeonHud` | `AgentMcpSampleDungeonHud` | Dungeon card with timer and progress, three objective row instances, boss health bar. |
| `WBP_DungeonResult` | `AgentMcpSampleDungeonResult` | Result card with three stat tile instances and a `DynamicEntryBox` that creates one reward slot per entry of `Rewards`. Plays the intro, whose timing, distances and scales are the `Motion` property. |
| `WBP_DungeonDemo` | `UserWidget` | Demo screen: a HUD instance and a result instance whose `Rewards` refer to five rows of the item table. |
| `Data/DA_DungeonUiTheme` | `AgentMcpSampleUiTheme` | Theme data asset: text and state colors, and a color and an optional frame brush per rarity. `Config/DefaultGame.ini` selects it. |
| `Data/DT_DungeonItems` | `AgentMcpSampleItemRow` | Item table: name, rarity, icon texture, and the shape drawn while the icon is missing. |

The C++ classes are in `Source/AgentMcpTestbed`. The texts are Korean; Roboto has no Hangul glyphs, so they render with the engine's
fallback font.

## Run it

Open the testbed as described in [Testbed and smoke test](../../README.md#testbed-and-smoke-test), then call:

```
pie_start
sample_show_widget {"widgetClass": "/Game/Samples/DungeonUi/WBP_DungeonDemo.WBP_DungeonDemo_C"}
viewport_capture
pie_stop
```

Wait about three seconds before `viewport_capture`, until the intro animation has finished.

## How it was built

### First version: one flat tree per screen

The request was a HUD and a result popup that look like a commercial game. Claude Code wrote C++ bases for data and animation and
built each screen with a single `umg_add_widgets` call (48 and 78 widgets), with brushes, fonts and slot layout inline. Looking at
the result found two problems:

- The first `viewport_capture` showed blue-tinted progress bars. `ProgressBar` multiplies its fill by `FillColorAndOpacity`, which
  defaults to blue. One `umg_set_widget_properties` call set it to white.
- The two `umg_add_widgets` results were about 30,000 and 52,000 characters, because the tools read back complete struct
  values; the second exceeded Claude Code's limit for tool output. The tools now read back only the requested fields. The change
  went in with Live Coding and was checked with the smoke test.

The user's review of that version: it looked finished, but nothing in it was reusable. The five reward slots, three stat tiles and
three objective rows were numbered copies, 38 brushes were written out inline, and the reward list could not follow the data.

### Where the fix belongs

Splitting repeated elements into components is a working practice, not a property of the tools, and studios do it differently.
So it went into a skill, [`umg-authoring`](../../Plugins/AgentMcp/Skills/umg-authoring/SKILL.md), rather than into the tool
descriptions that every client loads with every request. The tool descriptions keep to what the tools do, for example that an entry
class can be a Widget Blueprint, and name the skill. The skill started as a Claude Code skill in `.claude/skills`; the plugin now
serves it with `skills_get`, so Codex and other MCP clients read the same version.

### Second version: components, data and a demo screen

Following the skill:

1. **Plan.** Components: reward slot, stat tile, objective row. Data: the `Reward` struct, labels and values, done/total. Dynamic
   list: the rewards. Style tokens: `AgentMcpSampleStyle.h`, a header of color constants that the third version replaced.
2. **C++ bases for the components**, with `BindWidget` contracts, instance-editable inputs applied in `NativePreConstruct`, and
   setters. The screen bases bind the components by class and pass data into them. The new classes needed a build with the editor
   closed.
3. **Components.** `umg_create_widget_blueprint` without a root, so that the root widget can be added under its contract name, one
   `umg_add_widgets` call per component, `blueprint_compile`.
4. **Screens.** A dry run of `umg_remove_widgets` listed the copied subtrees (19 widgets in the HUD, 48 in the popup) and warned
   that removing `TimeTile` leaves a required `BindWidget` unbound until its replacement exists. The confirmed call removed them.
   `umg_add_widgets` then placed component instances with their labels as instance properties, and a `DynamicEntryBox` whose
   `EntryWidgetClass` is the reward slot.
5. **Demo screen** with a HUD instance and a result instance; its `Rewards` instance property holds five rewards with different
   rarities and icon shapes.
6. **Review.** `blueprint_compile` for every Widget Blueprint, components first; `pie_start`, `sample_show_widget`,
   `viewport_capture` and `pie_stop` for the HUD alone and for the demo screen; `asset_save`.

The smoke test gained checks for these paths: a Widget Blueprint instance with an instance property, a `DynamicEntryBox` entry
class, and the refusal to nest a Widget Blueprint in itself.

### Third version: presentation in data

The next review asked for decoration that changes without code: data, or Blueprints for simple cases, with icons and ornaments made
by an image model, another agent or an artist. Three tools were added for this: `asset_create` for data assets and DataTables,
`asset_import_textures`, and `object_set_properties` for project assets.

1. **C++ contracts.** The color constants became the theme data asset class, selected in the project settings, with the class
   defaults as its fallback. A reward became a row of the item table and a count. The reward slot got an optional `IconImage` and
   draws the item's fallback shape while there is no icon texture. The timing of the result intro became the `Motion` property. These
   were changes to reflected declarations, so the editor was closed for a build.
2. **Data.** `asset_create` made `DA_DungeonUiTheme` and `DT_DungeonItems`, and `datatable_add_rows` added the five items.
3. **Widgets.** `umg_add_widgets` put `IconImage` into the reward slot, and `umg_set_widget_properties` pointed the demo rewards at the
   table rows. The changed Widget Blueprints compiled without errors, and the play session capture looked like the second version.
4. **Check.** `object_set_properties` changed two colors of the theme: the accent and the legendary color. The next capture showed
   the open objective and the legendary reward slot in the new colors, without a build.

   ![The theme with another accent and legendary color](../Images/dungeon_theme_change.jpg)

## Not covered

- The intro is code in the C++ base, with its values in data; the tools cannot author UMG widget animations.
  [Docs/Experiments/WidgetAnimationAuthoring.md](../Experiments/WidgetAnimationAuthoring.md) plans an experiment for that.
- The icons, frames and card panel are still shapes, because the sample has no textures for them yet.
- The tools cannot move a widget to another parent, so the rework removed the copies and added instances instead.
