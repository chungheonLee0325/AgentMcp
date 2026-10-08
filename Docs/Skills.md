# Skills

[한국어](Skills.ko.md)

A skill is a task guide for agents: a folder with a `SKILL.md` file whose front matter contains a `name` and a `description`, matching the format used by Claude Code and Codex. The editor serves the skills, so every connected agent reads the same version and the guidance travels with the plugin.

- `initialize` lists skills and descriptions in the server instructions.
- `skills_list` returns discovered skills and reports invalid skill files.
- `skills_get` returns the guide and can also serve text files such as references, examples and scripts from inside the skill folder.

Skills are read from these folders in this order. A skill from a later folder replaces one with the same name from an earlier folder, allowing projects to adapt plugin guidance.

1. `Plugins/AgentMcp/Skills`: plugin skills, currently `umg-authoring`, `ui-art-requests` and `ui-style-system`
2. `AgentMcp/Skills` in the project folder
3. folders configured through `SkillDirectories`

Skill files are read on every call, so edits apply without restarting the editor. Only the short list included in server instructions is created when the server starts.

Claude Code and Codex choose skills by their descriptions. To let them automatically begin a served skill, put a short `SKILL.md` with the same name and description under `.claude/skills/<name>/` or `.agents/skills/<name>/` that tells the agent to call `skills_get`. This repository includes those bridges for all plugin skills.

Unreal Engine 5.8 also provides an Agent Skill concept. UE 5.8 uses `UAgentSkill` classes authored in C++, Python or Blueprint; Agent MCP instead implements Markdown skills so they are editable as text and share the same format as Claude Code and Codex skills.
