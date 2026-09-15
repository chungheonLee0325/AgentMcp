# UI style of the testbed samples

Draft of 2026-09-15, not approved yet. No kit piece is requested before the user approves it.

The style frame is the dungeon result mockup that the user made with ChatGPT on 2026-09-15. Save it as
`Art/Style/style_frame_dungeon_result.png`; requests list it in `references`. Take its structure and restraint, not its excess (see
"Keep out").

## Direction

A fantasy ruin lit by cyan arcane light. Dark, slightly transparent navy panels with thin cyan lines; gold only for the rank, currency
and bonuses; white text. Decoration marks structure: the edge of a panel, the title, the start of a section. The game scene is the
background, so panels stay calm.

## Tokens

Read from the style frame by eye. They are theme values in `DA_DungeonUiTheme`, not colors painted into textures.

| Token | Value | Use |
|---|---|---|
| Panel | #0A1626 at 88% | Panels, tiles, slots |
| Line | #3CCBFF | Panel borders, dividers, the primary button |
| LineDim | #3CCBFF at 35% | Outlines of tiles, slots and secondary buttons |
| Gold | #F2C14E | Rank, currency, first clear bonus |
| Text | #FFFFFF | Titles and values |
| MutedText | #9FB4C8 | Labels |
| Danger | #FF5A6E | Downs and warnings |

## Grid and lines at 1080p

- Spacing in steps of 4: panel padding 32, gaps between tiles 12, space between sections 20.
- Two line weights: 2 for panel borders, 1 for everything else.
- Corner radius 8 for panels, 6 for tiles, slots and buttons.
- One ornament motif: a small diamond, 7 x 7, at the inner ends of dividers and in the corner brackets of panels.

## Kit

| Piece | Made as | Notes |
|---|---|---|
| Panel | `RoundedBox` in Panel with a 2 unit Line outline; corner brackets as two 1 unit lines with a diamond | One for every popup |
| Title crest | Texture, 160 x 64 | Centered on the top edge of the panel, once per screen |
| Title and section divider | `Image` in Line, 1 unit high, fading out at the outer end, with a diamond at the inner end | On both sides of the title and of section labels |
| Stat tile | `RoundedBox` in Panel with a 1 unit LineDim outline; line icon on the left, label above the value | |
| Reward slot | `RoundedBox` in Panel with a 1 unit outline in the rarity color; count at the bottom right, name below | Rarity by tint, no frame image per rarity |
| Rank emblem | Texture, 256 x 256 per rank | Gold letter on a simple shield or ring |
| Bonus bar | `RoundedBox` in Panel with a 1 unit Gold outline | |
| Buttons | Primary: filled Line with dark text. Secondary: 1 unit LineDim outline with an icon and Text | Height 44 |
| Stat icons | Line icons, 32 x 32, 2 px stroke in Line or Gold | One set, one stroke |

## Item icons

- Painted, three-quarter view, light from the top left, a thin dark outline, a soft cyan rim light from the surroundings.
- The same saturation and level of detail across a set; no background, frame or text.
- Made as one batch with the style frame next to them, and remade together when one drifts.

## Keep out

- Glow on every element. Glow belongs to the primary button and the rank emblem.
- Ornaments on every edge, or a second ornament motif. The style frame's laurel wings, sparkle clouds and extra diamonds are left out.
- Painted, flat and photographic images next to each other.
- Busy textures inside panels, text in images, symbols that look like writing.
