---
name: perf-investigation
description: Measure, explain and fix performance in an Unreal game - CSV captures of fixed scenes in a standalone game, marked moments for stalls (opening a screen, a list refresh, a drag), Unreal Insights hotspots, ProfileGPU pass breakdowns, before/after comparison that respects run-to-run noise. Use when a screen hitches, a frame budget is exceeded, an optimization needs proof, or someone asks whether something is worth optimizing.
---

# Perf investigation

The Unreal Editor serves this skill through the Agent MCP server, so every agent and every project with the plugin reads the same
version.

1. Call the Agent MCP tool `skills_get` with `{"name": "perf-investigation"}` and follow the instructions it returns. Its `files` list
   names further files of the skill, which `skills_get` reads with the `file` argument.
2. If that tool is not available because the editor is not running, read `Plugins/AgentMcp/Skills/perf-investigation/SKILL.md` instead.
