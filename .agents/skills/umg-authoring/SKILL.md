---
name: umg-authoring
description: Build or restyle Unreal Engine UMG user interfaces (HUDs, menus, popups, lists, result screens) through the Agent MCP umg tools the way a production UI team works - reusable component Widget Blueprints for repeated elements, one place for style values, C++ BindWidget contracts, data-driven lists and a capture-based review. Use whenever a task calls umg_create_widget_blueprint, umg_add_widgets, umg_set_widget_properties or umg_remove_widgets, or asks for game UI in an Unreal project that has the Agent MCP server.
---

# UMG authoring

The Unreal Editor serves this skill through the Agent MCP server, so every agent and every project with the plugin reads the same
version.

1. Call the Agent MCP tool `skills_get` with `{"name": "umg-authoring"}` and follow the instructions it returns. Its `files` list
   names further files of the skill, which `skills_get` reads with the `file` argument.
2. If that tool is not available because the editor is not running, read `Plugins/AgentMcp/Skills/umg-authoring/SKILL.md` instead.
