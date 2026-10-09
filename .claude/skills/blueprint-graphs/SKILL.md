---
name: blueprint-graphs
description: Create Unreal Blueprint classes and read and edit their graphs (event graphs, functions, macros) through the Agent MCP blueprint tools - add variables, functions and event dispatchers, find node types, add and wire nodes, set pin values, compile and check the result in a play session. Use whenever a task calls blueprint_get_graph, blueprint_find_node_types, blueprint_edit_graph or blueprint_add_members, or asks to add or change Blueprint logic in an Unreal project that has the Agent MCP server.
---

# Blueprint graphs

The Unreal Editor serves this skill through the Agent MCP server, so every agent and every project with the plugin reads the same
version.

1. Call the Agent MCP tool `skills_get` with `{"name": "blueprint-graphs"}` and follow the instructions it returns. Its `files` list
   names further files of the skill, which `skills_get` reads with the `file` argument.
2. If that tool is not available because the editor is not running, read `Plugins/AgentMcp/Skills/blueprint-graphs/SKILL.md` instead.
