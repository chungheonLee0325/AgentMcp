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
- `Saved/ArtReview/<feature>.html`: the review sheet.
- In this skill: `references/request-example.json`, a complete request; `scripts/art_review.py`, which writes the review sheet; and
  `scripts/art_fit.py`, which fits an image to the size of an item. `skills_get` returns the folder of the skill as `path` and these
  files as `files`. Read the example with the `file` argument, and run the scripts from `path` with Python; they need only its
  standard library.

## Request format

The top level has `feature`, `description`, `style` (the look shared by every item, written once), `references` (captures or mockups),
`items`, and `captures` (the latest play session captures of the screen, once images are connected). Paths are relative to the project
folder. Each item has:

| Field | Meaning |
|---|---|
| `id` | File name without extension, in snake_case |
| `usage` | `icon`, `frame`, `panel`, `decoration` or `background` |
| `subject` | What the image shows |
| `size` | `[width, height]` in pixels, delivered exactly |
| `transparent` | The image needs an alpha channel |
| `padding` | Empty pixels around an icon, so that icons line up |
| `nineSlice` | `[left, top, right, bottom]` border in pixels of a frame or panel that stretches |
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
2. **Imported images:** do they work in the game? The user answers in the conversation. Write approvals as `approved`, and feedback
   such as "icon_golem_core: brighter" into `feedback` of that item with `status` `revise`.

No decision lies between making and importing, so an agent that can generate images may take both of those roles in one run.

## Review sheet

Run `python <path>/scripts/art_review.py Art/Requests/<feature>.json` in the project folder. It writes one HTML file with every item:
the image at its actual size, on the panel color and stretched as a 9-slice, the automatic checks (status, unique id and target, file,
size, alpha channel, padding, 9-slice border, record) and the next steps. Its output repeats the counts, the next steps and the errors:
errors of the request, and errors of images that are marked delivered or later. It exits with 1 when there is such an error. Run it at
the start of every run and after every change to the request or the images.

## Handoff

End every run with, in the language of the user:

1. What you did, item by item.
2. The counts per status and the errors that the review sheet reports.
3. The next steps: who does each, and the exact sentence to give them. At a decision point, what the user should check.
4. The path of the review sheet.

## Writing a request (UI agent)

- Request images only for elements whose look comes from data or assets: item icons, rarity frames, panels, badges and ornaments.
  Colors, spacing and simple rounded shapes stay in the theme and the Widget Blueprints.
- Keep variation in data, not in pixels: one icon per item without rarity colors or frames, and one frame per rarity. A rarity change
  then needs no new icon.
- Give frames and panels a `nineSlice` border.
- Choose `size` for the largest size on screen at 1080p; UI textures have no mipmaps.
- Write `usedBy` precisely, so that connecting the texture needs no guessing.
- Write the review sheet, fix its errors, and end the run at decision 1.

## Making the images (image maker)

- Follow `style` for every item: the same outline weight, light direction, palette and level of detail. No text, logos or watermarks.
- Deliver PNG files at exactly `size`, with a transparent background when `transparent` is true and clean alpha edges without a halo.
  Image models usually return other sizes: `python <path>/scripts/art_fit.py <image> Art/Requests/<feature>.json <id>` writes
  `Art/Incoming/<feature>/<id>.png`. It crops an item with `padding` to its visible pixels, scales it into `size` less the padding and
  centers it, and it scales any other item as a whole, so that the 9-slice border stays in place. It reads 8-bit PNG files.
- Keep the border of a `nineSlice` item inside the given pixels, and keep its middle even so that it can stretch.
- Write `<id>.json` next to each image, run the review sheet, fix what it reports, and only then set `status` to `delivered`. Leave the
  other fields to the UI agent.
- Use generated images only when the project or the user asked for them.

## Importing and connecting (UI agent)

1. Run the review sheet, and import only delivered items without errors; set items with errors back to `revise` with the reason as
   `feedback`.
2. `asset_import_textures` with the delivered files and the `target` paths, `bUserInterface` true. Replace revised images with
   `bReplaceExisting`.
3. Connect each texture where `usedBy` says:
   - a table cell with `datatable_set_rows`, for example
     `{"dataTable": "/Game/UI/Data/DT_Items.DT_Items", "rows": {"IronSword": {"Icon": "/Game/UI/Textures/Items/T_Icon_IronSword.T_Icon_IronSword"}}}`
   - a data asset property with `object_set_properties`, for example a 9-slice frame
     `{"object": "/Game/UI/Data/DA_UiTheme.DA_UiTheme", "values": {"Epic": {"Frame": {"ResourceObject": "<texture object path>", "DrawAs": "Box", "Margin": {"Left": 0.1875, "Top": 0.1875, "Right": 0.1875, "Bottom": 0.1875}, "ImageSize": {"X": 128, "Y": 128}}}}}`.
     `Margin` is a fraction of the texture: the `nineSlice` pixels divided by the width or height. `ImageSize` sets the size the
     border is drawn at: `Margin` times `ImageSize`, in Slate units.
   - a widget property with `umg_set_widget_properties`, then `blueprint_compile`
4. Review in a play session (`pie_start`, show the screen, `viewport_capture`, `pie_stop`): sharpness at play resolution, alpha edges,
   stretched 9-slice borders, consistency between items, and contrast against the panels. Put the capture paths into `captures`.
5. Set `status` to `imported`, save the textures and the changed data with `asset_save`, write the review sheet again, and end the run
   at decision 2.
