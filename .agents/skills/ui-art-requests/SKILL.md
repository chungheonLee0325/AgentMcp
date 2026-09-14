---
name: ui-art-requests
description: Request, make and connect art for Unreal UMG screens (item icons, slot frames, panels, decorations) through a shared JSON art request, whether an image model, another agent or an artist draws the images. Use when a screen needs textures that the project does not have yet, when requested images arrive, or when asked to write, continue, fill or review an art request.
---

# UI art requests

The Unreal Editor serves this skill through the Agent MCP server, so every agent and every project with the plugin reads the same
version.

1. Call the Agent MCP tool `skills_get` with `{"name": "ui-art-requests"}` and follow the instructions it returns. Its `files` list
   names further files of the skill, which `skills_get` reads with the `file` argument.
2. If that tool is not available because the editor is not running, read `Plugins/AgentMcp/Skills/ui-art-requests/SKILL.md` instead.
