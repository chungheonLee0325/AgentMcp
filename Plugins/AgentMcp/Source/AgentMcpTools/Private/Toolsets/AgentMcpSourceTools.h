#pragma once

#include "CoreMinimal.h"
#include "AgentMcpToolset.h"

#include "AgentMcpSourceTools.generated.h"

USTRUCT(BlueprintType)
struct FAgentMcpSourceFile
{
	GENERATED_BODY()

	/** Relative to the project folder, with forward slashes. */
	UPROPERTY()
	FString Path;

	UPROPERTY()
	int64 SizeBytes = 0;
};

USTRUCT(BlueprintType)
struct FAgentMcpSourceFindResult
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FAgentMcpSourceFile> Files;

	/** The readable folders, relative to the project folder. */
	UPROPERTY()
	TArray<FString> Folders;

	/** More files matched than were returned. */
	UPROPERTY()
	bool bTruncated = false;
};

USTRUCT(BlueprintType)
struct FAgentMcpSourceMatch
{
	GENERATED_BODY()

	UPROPERTY()
	FString Path;

	/** 1-based. */
	UPROPERTY()
	int32 Line = 0;

	/** The matching line, trimmed and cut at 300 characters. */
	UPROPERTY()
	FString Text;
};

USTRUCT(BlueprintType)
struct FAgentMcpSourceSearchResult
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FAgentMcpSourceMatch> Matches;

	UPROPERTY()
	int32 FilesSearched = 0;

	/** More lines matched than were returned, or the file limit was reached. */
	UPROPERTY()
	bool bTruncated = false;
};

USTRUCT(BlueprintType)
struct FAgentMcpSourceReadResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString Path;

	/** How the bytes were decoded: utf-8, utf-8-bom, utf-16 or cp949. */
	UPROPERTY()
	FString Encoding;

	UPROPERTY()
	int32 TotalLines = 0;

	UPROPERTY()
	int32 StartLine = 0;

	UPROPERTY()
	int32 EndLine = 0;

	/** The lines, each prefixed with its number. */
	UPROPERTY()
	FString Text;

	/** Lines after EndLine exist; read on with startLine = endLine + 1. */
	UPROPERTY()
	bool bTruncated = false;
};

USTRUCT(BlueprintType)
struct FAgentMcpSourceEditResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString Path;

	/** The encoding written: the file's own for an existing file (CP949 stays CP949), utf-8 for a new one. */
	UPROPERTY()
	FString Encoding;

	UPROPERTY()
	bool bCreated = false;

	/** Number of replaced occurrences, for source_replace. */
	UPROPERTY()
	int32 Replacements = 0;

	UPROPERTY()
	int32 TotalLines = 0;
};

/**
 * Reads the project's text files (C++, config, plugin sources) inside the folders allowed by the SourceFolders setting. Files saved
 * in CP949, as Korean projects often are, are decoded so their comments read correctly.
 */
UCLASS(meta = (McpToolset = "source"))
class UAgentMcpSourceTools : public UAgentMcpToolset
{
	GENERATED_BODY()

public:
	/**
	 * Finds text files by name in the readable project folders (Source, Config and Plugins by default).
	 * @param Pattern Wildcard matched against the file name, or against the relative path when it contains a slash, for example "*Quest*.h" or "Source/Game/Manager/*.h".
	 * @param MaxResults Maximum number of files to return (1-1000).
	 * @return Matching files relative to the project folder.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Source", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpSourceFindResult Find(const FString& Pattern, int32 MaxResults = 200);

	/**
	 * Searches the text files of the readable project folders line by line.
	 * @param Query Text to find; a regular expression when bRegex is true.
	 * @param FilePattern Optional wildcard that limits the files searched, matched like source_find's pattern, for example "*.h".
	 * @param bRegex Treat the query as a regular expression.
	 * @param bCaseSensitive Match case.
	 * @param MaxResults Maximum number of matching lines to return (1-500).
	 * @return Matching lines with their file and line number.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Source", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpSourceSearchResult Search(const FString& Query, const FString& FilePattern = TEXT(""), bool bRegex = false, bool bCaseSensitive = false, int32 MaxResults = 100);

	/**
	 * Reads lines of a text file in the readable project folders.
	 * @param Path Relative to the project folder (as source_find returns it) or absolute.
	 * @param StartLine First line to return, 1-based.
	 * @param LineCount Number of lines to return (1-2000).
	 * @return The lines with their numbers, the detected encoding and whether more lines follow.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Source", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
	static FAgentMcpSourceReadResult Read(const FString& Path, int32 StartLine = 1, int32 LineCount = 400);

	/**
	 * Replaces exact text in a text file of the readable project folders. The file keeps its encoding and line endings. Not undoable
	 * from the editor; use source control to revert.
	 * @param Path Relative to the project folder or absolute.
	 * @param OldText Text to replace, copied exactly from source_read without the line numbers. It must occur once unless bReplaceAll.
	 * @param NewText Replacement text.
	 * @param bReplaceAll Replace every occurrence instead of requiring exactly one.
	 * @return The file, its encoding and how many occurrences were replaced.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Source", meta = (AICallable, McpAccess = "Write", BlueprintInternalUseOnly = "true"))
	static FAgentMcpSourceEditResult Replace(const FString& Path, const FString& OldText, const FString& NewText, bool bReplaceAll = false);

	/**
	 * Writes a whole text file in the readable project folders, creating it when it is missing. An existing file keeps its encoding
	 * and line endings. Not undoable from the editor; use source control to revert.
	 * @param Path Relative to the project folder or absolute.
	 * @param Content The complete new content.
	 * @param bOverwrite Replace an existing file; without it an existing file is refused.
	 * @return The file, its encoding and whether it was created.
	 */
	UFUNCTION(BlueprintCallable, Category = "Agent MCP|Source", meta = (AICallable, McpAccess = "Write", BlueprintInternalUseOnly = "true"))
	static FAgentMcpSourceEditResult Write(const FString& Path, const FString& Content, bool bOverwrite = false);
};
