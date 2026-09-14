#pragma once

#include "CoreMinimal.h"

namespace UE::AgentMcp
{
	/** A skill: a folder whose SKILL.md starts with front matter that names and describes the skill. */
	struct FSkillInfo
	{
		FString Name;
		FString Description;
		/** Absolute path of the skill folder. */
		FString Directory;
		/** Folder of a skill with the same name, found earlier, that this skill replaces. */
		FString OverriddenDirectory;
	};

	struct FSkillScan
	{
		/** Usable skills, sorted by name. */
		TArray<FSkillInfo> Skills;
		/** Absolute paths of the skill folders in search order. A skill replaces one with the same name from an earlier folder. */
		TArray<FString> SearchedDirectories;
		/** Skill files that were skipped, with the reason. */
		TArray<FString> Problems;
	};

	/**
	 * Finds skills in the plugin's Skills folder, the project's AgentMcp/Skills folder and the folders of the SkillDirectories setting,
	 * in that order. The files are read again on every call.
	 */
	AGENTMCPTOOLSET_API FSkillScan ScanSkills();

	/**
	 * Splits the text of a SKILL.md file into the fields of its front matter and the body after it. Handles the YAML that skill front
	 * matter uses: plain, quoted and block (| and >) scalars. Returns false with a reason when the front matter is missing or not closed.
	 */
	AGENTMCPTOOLSET_API bool ParseSkillFile(const FString& Text, TMap<FString, FString>& OutFields, FString& OutBody, FString& OutError);

	/** The line of the server instructions that lists the skills, or an empty string when there are none. */
	AGENTMCPTOOLSET_API FString BuildSkillInstructions();
}
