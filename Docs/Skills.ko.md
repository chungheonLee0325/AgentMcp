# 스킬

[English](Skills.md)

스킬은 에이전트용 작업 지침입니다. `SKILL.md` 파일이 있는 폴더이고, 파일 앞머리(front matter)에 `name`과 `description`이 있습니다. Claude Code와 Codex가 자기 스킬에 쓰는 형식과 같습니다. 스킬은 에디터가 제공하므로 연결된 모든 에이전트가 같은 버전을 읽고, 플러그인을 옮기면 스킬도 따라갑니다.

- `initialize`가 돌려주는 서버 안내에 스킬마다 이름과 설명이 들어 있습니다.
- `skills_list`는 스킬과 그 폴더, 그리고 건너뛴 스킬 파일과 이유를 돌려줍니다.
- `skills_get`은 스킬의 지침과 폴더를 돌려주고, 요청하면 참고 자료나 예제 같은 딸린 파일도 돌려줍니다.

스킬은 다음 폴더에서 이 순서로 읽습니다. 뒤 폴더의 스킬이 앞 폴더의 같은 이름 스킬을 대신하므로, 프로젝트가 플러그인 스킬을 고쳐 쓸 수 있습니다.

1. `Plugins/AgentMcp/Skills`: 플러그인 스킬. 지금은 `umg-authoring`, `ui-art-requests`, `ui-style-system`, `blueprint-graphs`, `perf-investigation`
2. 프로젝트 폴더의 `AgentMcp/Skills`
3. `SkillDirectories` 설정의 폴더

파일은 호출할 때마다 새로 읽으므로 스킬을 고치면 에디터를 다시 시작하지 않아도 반영됩니다. 서버 안내에 들어가는 목록만 서버가 시작할 때 만들어집니다.

Claude Code와 Codex는 설명을 보고 스킬을 고릅니다. 서버 스킬도 스스로 시작하게 하려면, 같은 이름과 설명에 `skills_get`을 호출하라는 내용만 적은 짧은 `SKILL.md`를 Claude Code는 `.claude/skills/<이름>/`, Codex는 `.agents/skills/<이름>/`에 둡니다. 이 저장소에는 플러그인 스킬 모두 양쪽에 있습니다.

Unreal Engine 5.8도 Agent Skill 개념을 제공합니다. UE 5.8 쪽은 C++, Python, Blueprint로 정의한 `UAgentSkill`을 사용하고 Agent MCP는 Markdown 파일을 읽도록 별도로 구현했습니다. 그래서 Skill을 텍스트로 고칠 수 있고 Claude Code·Codex와 같은 형식을 공유합니다.
