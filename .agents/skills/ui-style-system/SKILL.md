---
name: ui-style-system
description: Set up, extract or check the visual style of an Unreal UMG project the way web teams run a design system - design tokens in a theme data asset, a kit gallery like Storybook, a style frame and capture comparisons. Use when a project needs a UI style or design tokens, when screens look inconsistent, when the UI of a game starts from existing Widget Blueprints or from a mockup image, or before restyling screens.
---

# UI style system

The Unreal Editor serves this skill through the Agent MCP server, so every agent and every project with the plugin reads the same
version.

1. Call the Agent MCP tool `skills_get` with `{"name": "ui-style-system"}` and follow the instructions it returns. Its `files` list
   names further files of the skill, which `skills_get` reads with the `file` argument.
2. If that tool is not available because the editor is not running, read `Plugins/AgentMcp/Skills/ui-style-system/SKILL.md` instead.
