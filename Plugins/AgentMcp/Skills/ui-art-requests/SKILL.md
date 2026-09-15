---
name: ui-art-requests
description: Request, make and connect art for Unreal UMG screens (item icons, slot frames, panels, decorations) through a shared JSON art request, whether an image model, another agent or an artist draws the images. Use when a screen needs textures that the project does not have yet, when requested images arrive, or when asked to write, continue, fill or review an art request.
---

# UI art requests

An art request is a JSON file that lists every image a screen needs. The agent that builds the UI writes it, whoever makes the images
fills it, and the UI agent imports the files and connects them to data. The request is the contract between them, so the workflow
stays the same whether an image model, another agent or an artist draws, and one agent can take several roles.

## Files

- `Art/Requests/<feature>.json` in the project folder: the request. It is outside `Content`, so the editor does not import it.
- `Art/Incoming/<feature>/<id>.png`: the delivered images, one per item, named by the item id. `<id>.json` next to each image records
  how it was made (tool or artist, model, prompt and settings, date, source files), for licensing and for making it again.
- `Art/Style/`: the art direction of the project: a style frame, which is one approved mockup of a key screen, and `ui_style.md` with
  the UI kit and its rules. See "Art direction".
- `Saved/ArtReview/<feature>.html`: the review sheet.
- In this skill: `references/request-example.json`, a complete request; `scripts/art_review.py`, which writes the review sheet; and
  `scripts/art_fit.py`, which fits an image to the size of an item. `skills_get` returns the folder of the skill as `path` and these
  files as `files`. Read the example with the `file` argument, and run the scripts from `path` with Python; they need only its
  standard library.

## Request format

The top level has `feature`, `description`, `style` (the look shared by every item, written once), `references` (the style frame,
captures or mockups), `items`, and `captures` (play session captures of the screen before and after the images were connected). Paths
are relative to the project folder. Each item has:

| Field | Meaning |
|---|---|
| `id` | File name without extension, in snake_case |
| `usage` | `icon`, `frame`, `panel`, `divider`, `ornament`, `decoration` or `background` |
| `subject` | What the image shows |
| `size` | `[width, height]` in pixels, delivered exactly |
| `transparent` | The image needs an alpha channel |
| `padding` | Empty pixels around an icon, so that icons line up. Frames and panels have none |
| `nineSlice` | `[left, top, right, bottom]` border in pixels of a frame or panel that stretches |
| `inset` | `[left, top, right, bottom]` content padding, in Slate units, of the widget that draws a 9-slice item |
| `target` | Package path of the texture, for example `/Game/UI/Textures/T_Icon_Sword` |
| `usedBy` | Where the texture is connected: a table row and column, a data asset property or a widget property |
| `status` | `requested`, `delivered`, `imported`, `approved` or `revise` |
| `feedback` | What to change, written when `status` is `revise` |

## Roles and decisions

Everyone continues a request with one sentence in any language, for example "Continue the art request dungeon_ui". Run the review
sheet, read the statuses, and do only the part of your role. When no item waits for your role, say so and name the role that is next.

| Role | Works on | Result |
|---|---|---|
| Image maker: an agent that can generate images, or an artist | `requested`, `revise` | `delivered` |
| UI agent | `delivered` | `imported` |
| Reviewer: the user | `imported` | `approved`, or `revise` with `feedback` |

The user decides at two points. Every run ends there, also when one agent could take the roles before and after:

1. **A new or changed request:** are these the right images, sizes and style? The user passes the request on to the image maker or
   asks for changes.
2. **Imported images:** do they work in the game? The user answers in the conversation. For approvals, save the textures and the
   changed data with `asset_save` and write `approved`. Write feedback such as "icon_golem_core: brighter" into `feedback` of that
   item with `status` `revise`.

No decision lies between making and importing, so an agent that can generate images may take both of those roles in one run.

## Review sheet

Run `python <path>/scripts/art_review.py Art/Requests/<feature>.json` in the project folder. It writes one HTML file with every item:
the image at its actual size, on the panel color and stretched as a 9-slice with its borders as Slate draws them, the automatic checks
and the next steps. The checks cover the request (status, unique id and target, size, 9-slice border, inset) and each image (file,
size, alpha channel, icon padding, a 9-slice border that reaches the image edge and ends inside its 9-slice border and the inset,
record). Its output repeats the counts, the next steps and the errors: errors of the request, and errors of images that are marked
delivered or later. It exits with 1 when there is such an error. Run it at the start of every run and after every change to the
request or the images.

## Handoff

End every run with, in the language of the user:

1. What you did, item by item.
2. The counts per status and the errors that the review sheet reports.
3. The next steps: who does each, and the exact sentence to give them. At a decision point, what the user should check.
4. The path of the review sheet.

## Art direction

A UI looks made by people when its pieces agree with each other, not when a single image is detailed. Items requested one by one with
only a text style each look fine and do not belong together.

- **Style frame first.** Agree on one mockup of a key screen, drawn or generated, before any kit piece or icon is made. Keep it in
  `Art/Style` and list it in `references` of every request. The look is each game's own; these rules are how to keep it.
- **A small UI kit instead of decoration per screen.** Take the few pieces that the style frame repeats: panel, section divider,
  corner ornament, title plate, tile or slot, button states. Describe them in `Art/Style/ui_style.md` with sizes, line weights, corner
  radii, 9-slice borders and the theme colors that tint them. Screens combine kit pieces.
- **Draw what the engine can draw.** Lines, dividers, outlines, rounded boxes, gradients and small diamonds are brushes with theme
  colors: a `RoundedBox` brush, an `Image` with a color, a small `Border` rotated by 45 degrees. They stay consistent and change without
  new art. Request textures only for what shapes cannot draw: icons, emblems, crests and ornate corners.
- **Vary by tint and data.** A neutral frame tinted with the rarity color rather than one painting per rarity, unless the style frame
  needs more.
- **Icons as a set.** Make the icons of a set in one session or on one sheet with the style frame as reference, fit each, and review
  them side by side. When one drifts, remake it with the others next to it.
- **What makes a UI look made:** one spacing grid, aligned edges, one or two line weights, one ornament motif repeated only at
  structural points (panel corners, section dividers, the title), glow only where the eye should go, hierarchy by size and contrast.
- **What makes it look generated:** ornaments on every edge, glow on everything, painted, flat and photographic images next to each
  other, decoration that marks nothing, symbols that look like writing.

## Writing a request (UI agent)

- Read `Art/Style/ui_style.md` first. Request kit pieces and item art from it; when a screen needs a new piece, propose it for the kit
  instead of requesting decoration for one screen.
- Request images only for elements whose look comes from data or assets: item icons, emblems and kit textures. Colors, spacing and
  shapes the engine draws stay in the theme and the Widget Blueprints.
- Keep variation in data, not in pixels: one icon per item without rarity colors or frames, and frames tinted by the theme. A rarity
  change then needs no new image.
- For a frame or panel, look at its widget with `umg_inspect` first and write the widget's content padding as `inset`. Slate draws
  9-slice margins at the texture's own pixel size, in Slate units, whatever `ImageSize` says (UE 5.5 `ElementBatcher.cpp`). Choose
  `size` and `nineSlice` for the border as it should appear at 1080p: a slot frame with a 12 unit border is a 64 x 64 image with
  `nineSlice` 12, not a 128 x 128 image with 24. The border starts at the image edge, with no transparent margin, and ends inside
  `inset`.
- A `Border` with a texture brush draws only that texture. When a frame goes over a fill, the widget needs a fill layer and a frame
  layer (see the `umg-authoring` skill).
- Choose the size of other images for the largest size on screen at 1080p; UI textures have no mipmaps.
- Write `usedBy` precisely, so that connecting the texture needs no guessing.
- Write the review sheet, fix its errors, and end the run at decision 1.

## Making the images (image maker)

- Follow the style frame and `style` for every item: the same outline weight, light direction, palette and level of detail. No text,
  logos or watermarks.
- Deliver PNG files at exactly `size`, with a transparent background when `transparent` is true and clean alpha edges without a halo.
  Image models usually return other sizes: `python <path>/scripts/art_fit.py <image> Art/Requests/<feature>.json <id>` writes
  `Art/Incoming/<feature>/<id>.png`. It crops a 9-slice item to its visible pixels and stretches it to `size`, so that its border
  reaches the edge; it crops an icon to its visible pixels, scales it into `size` less the padding and centers it; and it scales any
  other item as a whole. It reads 8-bit PNG files.
- Keep the ornament of a `nineSlice` item inside the given border and inside `inset`, and keep its middle even so that it can stretch.
- Write `<id>.json` next to each image, run the review sheet, fix what it reports, and only then set `status` to `delivered`. Leave the
  other fields to the UI agent.
- Use generated images only when the project or the user asked for them.

## Importing and connecting (UI agent)

1. Run the review sheet, and import only delivered items without errors; set items with errors back to `revise` with the reason as
   `feedback`.
2. Capture the screen before connecting anything: `pie_start`, show the screen, `viewport_capture`, `pie_stop`.
3. `asset_import_textures` with the delivered files and the `target` paths, `bUserInterface` true. Replace revised images with
   `bReplaceExisting`.
4. Connect each texture where `usedBy` says:
   - a table cell with `datatable_set_rows`, for example
     `{"dataTable": "/Game/UI/Data/DT_Items.DT_Items", "rows": {"IronSword": {"Icon": "/Game/UI/Textures/Items/T_Icon_IronSword.T_Icon_IronSword"}}}`
   - a data asset property with `object_set_properties`, for example a 9-slice frame
     `{"object": "/Game/UI/Data/DA_UiTheme.DA_UiTheme", "values": {"Epic": {"Frame": {"ResourceObject": "<texture object path>", "DrawAs": "Box", "Margin": {"Left": 0.1875, "Top": 0.1875, "Right": 0.1875, "Bottom": 0.1875}, "TintColor": {"SpecifiedColor": {"R": 1, "G": 1, "B": 1, "A": 1}}}}}}`.
     `Margin` is the `nineSlice` pixels divided by the width or height. Set `TintColor` to white when the brush had a tint.
   - a widget property with `umg_set_widget_properties`, then `blueprint_compile`
5. Capture the screen again at the same viewport size and put both captures side by side before judging any image. Every panel and
   frame still surrounds its content, nothing moved or overlaps, and the new images agree with the style frame and with each other
   (line weight, palette, light, level of detail). Then look at sharpness at play resolution, alpha edges and stretched borders. Put
   both capture paths into `captures`.
6. When the layout broke or the images do not belong together, undo the connections with `editor_undo`, set the items to `revise`
   with the measured reason as `feedback`, and fix the request, the kit or the widget before importing again.
7. Set `status` to `imported`, write the review sheet again, and end the run at decision 2. Save after the user approves.
