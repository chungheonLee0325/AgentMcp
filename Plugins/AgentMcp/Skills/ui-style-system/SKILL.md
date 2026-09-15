---
name: ui-style-system
description: Set up, extract or check the visual style of an Unreal UMG project the way web teams run a design system - design tokens in a theme data asset, a kit gallery like Storybook, a style frame and capture comparisons. Use when a project needs a UI style or design tokens, when screens look inconsistent, when the UI of a game starts from existing Widget Blueprints or from a mockup image, or before restyling screens.
---

# UI style system

A UI style is settled by building it, not by writing it down first. It lives as design tokens in a theme data asset, is seen in a
kit gallery, and is judged against a style frame and captures. The user decides twice: the direction and the freeze. Everything
between is scripts, the theme and captures, not conversation.

## Web practice and its Unreal counterpart

| Web | Unreal |
|---|---|
| Design tokens: named colors, spacing, radii, type sizes | Theme data asset that the widgets and their C++ bases read |
| CSS classes | Styled widgets: a border, text, bar or button with a `Style` name that it looks up in the theme; CommonUI style classes where the project uses CommonUI |
| Component library | Component Widget Blueprints built from styled widgets |
| Storybook | Kit gallery: one Widget Blueprint with every token and every component in every state |
| Visual regression tests (Chromatic, Playwright screenshots) | Play captures of the gallery and key screens against a baseline: `scripts/capture_compare.py` |
| Token audit and lint (CSS Stats, stylelint) | `scripts/style_extract.py` |
| Theme from an image (Material Theme Builder) | `scripts/style_palette.py` |

Tokens have three tiers: primitive colors (the palette), semantic tokens (Panel, Line, Text, MutedText, Accent, Danger, rarity colors,
corner radii, line widths, spacing steps, type sizes) and component values that refer to semantic tokens. Widgets use semantic tokens;
a new screen brings no colors of its own.

The scripts are in `scripts/` of this skill (`path` in the `skills_get` result) and need only Python's standard library. They write to
`Saved/UiStyle` of the folder they run in, so run them in the project folder. In Git Bash, set `MSYS_NO_PATHCONV=1`, or `/Game`
arguments turn into Windows paths.

## 1. Find the starting point

- **Existing Widget Blueprints:** `python <path>/scripts/style_extract.py --url <Agent MCP URL> --path /Game/UI --theme <theme object path>`
  reads every Widget Blueprint under the folder and the theme through the server. The report lists which widgets use a style of the
  theme and which carry their own values, color clusters with where they are used and whether the theme has them, near duplicates, the
  type scale, corner radii, line widths, spacing with its fit to a 4 and an 8 unit grid, fixed sizes and textures. Values are counted as
  Slate draws them: an outline with `bUseBrushTransparency`, the default of UMG buttons, takes the opacity of its fill instead of its
  own. Colors that code sets at runtime show only as their placeholders in the Widget Blueprints.
- **A style frame:** keep the mockup as PNG in `Art/Style/` and run `python <path>/scripts/style_palette.py Art/Style/<frame>.png` for
  its surfaces, lights and accents as sRGB and as the linear values Unreal colors store. `--compare Saved/UiStyle/<name>.json` puts the
  closest existing color and its token next to each.
- **Both:** give the user both reports and one question: keep the existing look and clean it up, or move toward the style frame.

## 2. Build the kit

- **Tokens and styles in data.** Keep maps from names to values in the theme data asset: colors (with opacity) and corner radii as
  tokens, and styles made of token names: a box (fill, outline, width, radius, padding), a text style (font, typeface, size, color,
  shadow, outline), a bar (track and fill) and a button (a box per state). Adding or changing a token or a style is then an
  `object_set_properties` call; only a new kind of style changes a reflected class and needs a build with the editor closed. Keep one
  value per role, and give a style an opacity rather than adding a lighter copy of a color.
- **Styled widgets.** Give each drawn widget class a small C++ subclass with a `Style` name, and a `Color` token for text, that applies
  the style from the theme in `SynchronizeProperties` before calling the parent. Widget Blueprints then store names instead of brushes
  and fonts, the designer shows the theme, and a theme change restyles every screen. Component code still reads colors by token for
  states such as rarity, done or warning. CommonUI's text, border and button styles do the same in CommonUI projects, but they are
  Blueprint classes that the tools cannot create or edit.
- **Moving existing screens.** The tools cannot change the class of a widget, so a screen moves to styled widgets by rebuilding its
  tree: read it with `umg_inspect` and `bIncludeProperties`, map every border, text, bar and button to a style, drop the values the
  style provides, snap paddings to the grid, then `umg_remove_widgets` on the root, `umg_add_widgets` with the same names so that
  `BindWidget` contracts stay bound, and `blueprint_compile`. If adding fails, call `editor_undo` and check with `umg_inspect` that the
  old tree is back. Capture the screen before and after as in section 4, and explain every difference before saving.
- **Kit gallery.** One Widget Blueprint that shows the whole kit, the project's Storybook. Let its C++ base fill panels from the theme
  (a swatch per color, a line per text style, a sample per box, bar and button style with its states), so that a new token appears
  without editing the gallery, and place component instances next to them in their states: every rarity, open and done, long and
  short texts. A capture cannot scroll: size the gallery for the play viewport and put it in a `ScaleBox` that only scales down.
- Capture the gallery in a play session and put it next to the style frame. Tune by changing theme values and capturing again.

## 3. Freeze

When the user approves the gallery capture:

- keep it and the key screens as the baseline in `Art/Style/baseline/`;
- write `Art/Style/ui_style.md` from the theme values, plus the few rules a person decides: the ornament motif, where glow is allowed,
  what is left out;
- list the style frame and the baseline in `references` of art requests (see the `ui-art-requests` skill).

## 4. After the freeze

- New screens use tokens, styled widgets and kit components. A new component goes into the gallery first.
- After every change to the theme, a component or a screen, capture the gallery and the touched screens in a play window of the
  baseline's size (`pie_start` with `windowWidth` and `windowHeight`; the level viewport follows the editor window and renders edges
  slightly differently)
  and run `python <path>/scripts/capture_compare.py <baseline>.png <capture>.png --region <left> <top> <right> <bottom>`. Capture after
  intro animations have finished: a capture taken during a fade looks like a broken style. Keep the region away from timers and other
  changing parts. Replace the baseline only when the user approves the change.
- Run `style_extract.py` again before a restyle or a release: widgets without a style, colors outside the theme and near duplicates are
  style debt.
