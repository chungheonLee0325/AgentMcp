#include "AgentMcpSkillTools.h"

#include "AgentMcpSettings.h"
#include "AgentMcpSkills.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace UE::AgentMcp::SkillToolsPrivate
{
	constexpr int32 MaxListedFiles = 200;
	constexpr int64 MaxFileBytes = 1024 * 1024;

	void LimitContent(FString& Content, bool& bOutTruncated)
	{
		// Leave room below the result limit for the other fields and JSON escaping.
		const int32 MaxCharacters = FMath::Max(512, GetDefault<UAgentMcpSettings>()->MaxResultBytes * 3 / 4);
		if (Content.Len() > MaxCharacters)
		{
			Content.LeftInline(MaxCharacters);
			bOutTruncated = true;
		}
	}

	bool HasHiddenSegment(const FString& RelativePath)
	{
		TArray<FString> Segments;
		RelativePath.ParseIntoArray(Segments, TEXT("/"));
		return Segments.ContainsByPredicate([](const FString& Segment) { return Segment.StartsWith(TEXT(".")); });
	}

	TArray<FString> ListOtherFiles(const FString& SkillDirectory)
	{
		TArray<FString> Found;
		IFileManager::Get().FindFilesRecursive(Found, *SkillDirectory, TEXT("*"), /*Files=*/true, /*Directories=*/false);

		const FString Prefix = SkillDirectory + TEXT("/");
		TArray<FString> Files;
		for (FString Path : Found)
		{
			FPaths::NormalizeFilename(Path);
			if (!Path.StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				continue;
			}
			FString Relative = Path.RightChop(Prefix.Len());
			if (!Relative.Equals(TEXT("SKILL.md"), ESearchCase::IgnoreCase) && !HasHiddenSegment(Relative))
			{
				Files.Add(MoveTemp(Relative));
			}
		}
		Files.Sort();
		if (Files.Num() > MaxListedFiles)
		{
			Files.SetNum(MaxListedFiles);
		}
		return Files;
	}
}

FAgentMcpSkillListResult UAgentMcpSkillTools::List()
{
	const UE::AgentMcp::FSkillScan Scan = UE::AgentMcp::ScanSkills();

	FAgentMcpSkillListResult Result;
	for (const UE::AgentMcp::FSkillInfo& Skill : Scan.Skills)
	{
		FAgentMcpSkillSummary& Summary = Result.Skills.AddDefaulted_GetRef();
		Summary.Name = Skill.Name;
		Summary.Description = Skill.Description;
		Summary.Path = Skill.Directory;
		Summary.Overrides = Skill.OverriddenDirectory;
	}
	Result.SearchedDirectories = Scan.SearchedDirectories;
	Result.Problems = Scan.Problems;
	return Result;
}

FAgentMcpSkillReadResult UAgentMcpSkillTools::Get(const FString& Name, const FString& File)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::SkillToolsPrivate;

	const FString SkillName = Name.TrimStartAndEnd();
	const FSkillScan Scan = ScanSkills();
	const FSkillInfo* Skill = Scan.Skills.FindByPredicate([&SkillName](const FSkillInfo& Each) { return Each.Name.Equals(SkillName, ESearchCase::IgnoreCase); });
	if (!Skill)
	{
		TArray<FString> Names;
		for (const FSkillInfo& Each : Scan.Skills)
		{
			Names.Add(Each.Name);
		}
		RaiseToolError(TEXT("NOT_FOUND"), FString::Printf(TEXT("No skill is named '%s'."), *SkillName),
			Names.IsEmpty()
				? FString(TEXT("No skills were found. skills_list shows the searched folders and the skill files that were skipped."))
				: FString::Printf(TEXT("Available skills: %s. skills_list shows when to use each."), *FString::Join(Names, TEXT(", "))));
		return FAgentMcpSkillReadResult();
	}

	FAgentMcpSkillReadResult Result;
	Result.Name = Skill->Name;
	Result.Description = Skill->Description;
	Result.Path = Skill->Directory;
	Result.Files = ListOtherFiles(Skill->Directory);

	const FString RelativeFile = File.TrimStartAndEnd().Replace(TEXT("\\"), TEXT("/"));
	if (RelativeFile.IsEmpty() || RelativeFile.Equals(TEXT("SKILL.md"), ESearchCase::IgnoreCase))
	{
		FString Text;
		TMap<FString, FString> Fields;
		FString Error;
		if (!FFileHelper::LoadFileToString(Text, *(Skill->Directory / TEXT("SKILL.md"))))
		{
			Error = TEXT("the file cannot be read");
		}
		else
		{
			ParseSkillFile(Text, Fields, Result.Content, Error);
		}
		if (!Error.IsEmpty())
		{
			RaiseToolError(TEXT("NOT_AVAILABLE"), FString::Printf(TEXT("The SKILL.md of '%s' cannot be used: %s."), *Skill->Name, *Error),
				TEXT("The file changed after it was listed; call skills_list again."));
			return FAgentMcpSkillReadResult();
		}
		Result.File = TEXT("SKILL.md");
		LimitContent(Result.Content, Result.bTruncated);
		return Result;
	}

	TArray<FString> Segments;
	RelativeFile.ParseIntoArray(Segments, TEXT("/"), /*InCullEmpty=*/false);
	const bool bOutsideSkill = RelativeFile.StartsWith(TEXT("/")) || !FPaths::IsRelative(RelativeFile)
		|| Segments.ContainsByPredicate([](const FString& Segment) { return Segment.IsEmpty() || Segment == TEXT(".") || Segment == TEXT(".."); });
	if (bOutsideSkill)
	{
		RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("file '%s' must be a path inside the skill folder, relative to it."), *File),
			TEXT("Use a path from files, for example references/example.md."));
		return FAgentMcpSkillReadResult();
	}
	if (HasHiddenSegment(RelativeFile))
	{
		RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("file '%s' is hidden: files and folders whose name starts with a dot are not served."), *File));
		return FAgentMcpSkillReadResult();
	}

	const FString FullPath = FPaths::ConvertRelativePathToFull(Skill->Directory / RelativeFile);
	if (!FullPath.StartsWith(Skill->Directory + TEXT("/"), ESearchCase::IgnoreCase) || !IFileManager::Get().FileExists(*FullPath))
	{
		RaiseToolError(TEXT("NOT_FOUND"), FString::Printf(TEXT("Skill '%s' has no file '%s'."), *Skill->Name, *RelativeFile),
			Result.Files.IsEmpty()
				? FString(TEXT("The skill has no files besides SKILL.md."))
				: FString::Printf(TEXT("Its files: %s."), *FString::Join(Result.Files, TEXT(", "))));
		return FAgentMcpSkillReadResult();
	}

	const int64 Size = IFileManager::Get().FileSize(*FullPath);
	if (Size > MaxFileBytes)
	{
		RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("file '%s' has %lld bytes; skill files above %lld bytes are not served."), *RelativeFile, Size, MaxFileBytes));
		return FAgentMcpSkillReadResult();
	}
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *FullPath))
	{
		RaiseToolError(TEXT("NOT_AVAILABLE"), FString::Printf(TEXT("file '%s' cannot be read."), *RelativeFile));
		return FAgentMcpSkillReadResult();
	}
	if (Bytes.Contains(0))
	{
		RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("file '%s' is not a text file."), *RelativeFile),
			TEXT("skills_get returns text files such as Markdown, JSON or scripts."));
		return FAgentMcpSkillReadResult();
	}

	FFileHelper::BufferToString(Result.Content, Bytes.GetData(), Bytes.Num());
	Result.File = RelativeFile;
	LimitContent(Result.Content, Result.bTruncated);
	return Result;
}
