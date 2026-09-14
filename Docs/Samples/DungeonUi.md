# Dungeon UI sample

[한국어](DungeonUi.ko.md)

A dungeon progress HUD and a dungeon result popup in the style of a creature-collecting survival game. Claude Code (desktop app,
version 2.1.270) built them in the Agent MCP testbed through the `umg` tools; nobody opened the UMG designer. This page records how,
including the first version that the review rejected, so that the workflow can be judged and not only the screenshots.

![Result popup over the HUD](../Images/dungeon_result.jpg)

![Dungeon progress HUD](../Images/dungeon_hud.jpg)

## Contents

| Asset under `/Game/Samples/DungeonUi` | C++ base | Role |
|---|---|---|
| `Components/WBP_RewardSlot` | `AgentMcpSampleRewardSlot` | Reward slot with icon, name and count. The `Reward` input (item name, count, rarity, icon shape) sets the rarity colors and the icon shape. |
| `Components/WBP_StatTile` | `AgentMcpSampleStatTile` | Label above a large value: `Label`, `Value`, `SetValue`. |
| `Components/WBP_ObjectiveRow` | `AgentMcpSampleObjectiveRow` | Check mark, label and done/total. Open objectives use `AccentColor`; completed ones turn green. |
| `WBP_DungeonHud` | `AgentMcpSampleDungeonHud` | Dungeon card with timer and progress, three objective row instances, boss health bar. |
| `WBP_DungeonResult` | `AgentMcpSampleDungeonResult` | Result card with three stat tile instances and a `DynamicEntryBox` that creates one reward slot per entry of `Rewards`. Plays the intro: backdrop, card pop-up, rank stamp, rewards one after another, experience bar with level-up. |
| `WBP_DungeonDemo` | `UserWidget` | Demo screen: a HUD instance and a result instance whose `Rewards` instance property holds five demo rewards. |

The C++ bases and the color tokens (`AgentMcpSampleStyle.h`) are in `Source/AgentMcpTestbed`. The texts are Korean; Roboto has no
Hangul glyphs, so they render with the engine's fallback font.

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
So it went into a Claude Code skill, [`.claude/skills/umg-authoring`](../../.claude/skills/umg-authoring/SKILL.md), rather than into
the tool descriptions that every client loads with every request. The tool descriptions keep to what the tools do, for example that
an entry class can be a Widget Blueprint.

### Second version: components, data and a demo screen

Following the skill:

1. **Plan.** Components: reward slot, stat tile, objective row. Data: the `Reward` struct, labels and values, done/total. Dynamic
   list: the rewards. Style tokens: `AgentMcpSampleStyle.h`.
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

## Not covered

- The animations are code in the C++ bases; the tools cannot author UMG widget animations.
- The icons are brush shapes, not textures.
- The tools cannot move a widget to another parent, so the rework removed the copies and added instances instead.
