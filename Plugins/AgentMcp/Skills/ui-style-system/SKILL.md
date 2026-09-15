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
| Component library | Component Widget Blueprints; CommonUI text, button and border style classes where the project uses CommonUI |
| Storybook | Kit gallery: one Widget Blueprint with every component in every state |
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
  reads every Widget Blueprint under the folder and the theme through the server. The report lists color clusters with where they are
  used and whether the theme has them, near duplicates, the type scale, corner radii, line widths, spacing with its fit to a 4 and an
  8 unit grid, fixed sizes and textures. Colors that code sets at runtime show only as their placeholders in the Widget Blueprints.
- **A style frame:** keep the mockup as PNG in `Art/Style/` and run `python <path>/scripts/style_palette.py Art/Style/<frame>.png` for
  its surfaces, lights and accents as sRGB and as the linear values Unreal colors store. `--compare Saved/UiStyle/<name>.json` puts the
  closest existing color and its token next to each.
- **Both:** give the user both reports and one question: keep the existing look and clean it up, or move toward the style frame.

## 2. Build the kit

- Put the chosen tokens into the theme data asset. New token properties change a reflected class and need a build with the editor
  closed. Components read the theme instead of their own values.
- Build a kit gallery Widget Blueprint with the umg tools: panel, section divider, tiles, slots in every rarity, buttons in every state,
  text styles, long and short texts, empty and full lists. It is the project's Storybook.
- Capture the gallery in a play session and put it next to the style frame. Tune by changing theme values and capturing again.

## 3. Freeze

When the user approves the gallery capture:

- keep it and the key screens as the baseline in `Art/Style/baseline/`;
- write `Art/Style/ui_style.md` from the theme values, plus the few rules a person decides: the ornament motif, where glow is allowed,
  what is left out;
- list the style frame and the baseline in `references` of art requests (see the `ui-art-requests` skill).

## 4. After the freeze

- New screens use tokens and kit components. A new component goes into the gallery first.
- After every change to the theme, a component or a screen, capture the gallery and the touched screens at the baseline's viewport size
  and run `python <path>/scripts/capture_compare.py <baseline>.png <capture>.png --region <left> <top> <right> <bottom>`. Keep the region
  away from timers and other changing parts. Replace the baseline only when the user approves the change.
- Run `style_extract.py` again before a restyle or a release: colors outside the theme and near duplicates are style debt.
