#pragma once

#include "CoreMinimal.h"
#include "AgentMcpToolset.h"

#include "AgentMcpSkillTools.generated.h"

USTRUCT(BlueprintType)
struct FAgentMcpSkillSummary
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	/** What the skill covers and when to use it. */
	UPROPERTY()
	FString Description;

	/** Folder of the skill. */
	UPROPERTY()
	FString Path;

	/** Folder of a skill with the same name that this one replaces. */
	UPROPERTY()
	FString Overrides;
};

USTRUCT(BlueprintType)
struct FAgentMcpSkillListResult
{
	GENERATED_BODY()

	/** Skills sorted by name. */
	UPROPERTY()
	TArray<FAgentMcpSkillSummary> Skills;

	/** Folders that are searched, in order. A skill replaces one with the same name from an earlier folder. */
	UPROPERTY()
	TArray<FString> SearchedDirectories;

	/** Skill files that were skipped, with the reason. */
	UPROPERTY()
	TArray<FString> Problems;
};

USTRUCT(BlueprintType)
struct FAgentMcpSkillReadResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString Description;

	/** Folder of the skill. */
	UPROPERTY()
	FString Path;

	/** File that content comes from, relative to the skill folder. */
	UPROPERTY()
	FString File;

	/** The instructions of SKILL.md without its front matter, or the text of the requested file. */
	UPROPERTY()
	FString Content;

	/** Content was cut off at the result size limit. */
	UPROPERTY()
	bool bTruncated = false;

	/** Other files of the skill, relative to its folder, which the file argument reads. */
	UPROPERTY()
	TArray<FString> Files;
};

/** Skills: task guides for agents, served from the SKILL.md files of the plugin and the project. */
UCLASS(meta = (McpToolset = "skills"))
class UAgentMcpSkillTools : public UAgentMcpToolset
{
	GENERATED_BODY()

public:
	/**
	 * Lists the skills this editor serves: task guides for agents, such as how to build game UI with the umg tools. Before a task that
	 * matches a skill's description, read the skill with skills_get. Skills are SKILL.md files in subfolders of the plugin's Skills folder,
	 * the project's AgentMcp/Skills folder and the SkillDirectories setting; a skill replaces one with the same name found earlier. The
	 * files are read on every call.
	 * @return Skills with their descriptions and folders, the searched folders in order, and the skill files that were skipped.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Skills", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpSkillListResult List();

	/**
	 * Reads a skill: the instructions of its SKILL.md without the front matter and the names of its other files, or one of those files.
	 * @param Name Skill name from skills_list, for example umg-authoring.
	 * @param File File of the skill to read instead of SKILL.md, relative to the skill folder, as listed in files.
	 * @return The instructions or the file text, with the other files of the skill.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Skills", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpSkillReadResult Get(const FString& Name, const FString& File = TEXT(""));
};
