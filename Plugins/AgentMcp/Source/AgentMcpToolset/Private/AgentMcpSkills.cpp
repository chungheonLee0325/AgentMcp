#include "AgentMcpSkills.h"

#include "AgentMcpSettings.h"

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace UE::AgentMcp::SkillsPrivate
{
	// Limits of the Agent Skills format.
	constexpr int32 MaxNameLength = 64;
	constexpr int32 MaxDescriptionLength = 1024;

	/** Above this many characters the server instructions list only the skill names. */
	constexpr int32 MaxInstructionsCharacters = 3000;

	FString MakeFullDirectory(const FString& Path)
	{
		FString Full = FPaths::ConvertRelativePathToFull(Path);
		FPaths::NormalizeDirectoryName(Full);
		return Full;
	}

	bool IsValidSkillName(const FString& Name)
	{
		if (Name.IsEmpty() || Name.Len() > MaxNameLength || Name.StartsWith(TEXT("-")) || Name.EndsWith(TEXT("-")) || Name.Contains(TEXT("--")))
		{
			return false;
		}
		for (const TCHAR Char : Name)
		{
			if (!((Char >= TEXT('a') && Char <= TEXT('z')) || (Char >= TEXT('0') && Char <= TEXT('9')) || Char == TEXT('-')))
			{
				return false;
			}
		}
		return true;
	}

	int32 CountIndent(const FString& Line)
	{
		int32 Indent = 0;
		while (Indent < Line.Len() && (Line[Indent] == TEXT(' ') || Line[Indent] == TEXT('\t')))
		{
			++Indent;
		}
		return Indent;
	}

	/** YAML starts a comment at a # that follows whitespace. */
	FString StripComment(const FString& Value)
	{
		for (int32 Index = 0; Index < Value.Len(); ++Index)
		{
			if (Value[Index] == TEXT('#') && (Index == 0 || FChar::IsWhitespace(Value[Index - 1])))
			{
				return Value.Left(Index);
			}
		}
		return Value;
	}

	/** Value starts with a double quote. */
	FString UnquoteDouble(const FString& Value)
	{
		FString Result;
		for (int32 Index = 1; Index < Value.Len(); ++Index)
		{
			const TCHAR Char = Value[Index];
			if (Char == TEXT('"'))
			{
				break;
			}
			if (Char == TEXT('\\') && Index + 1 < Value.Len())
			{
				const TCHAR Escaped = Value[++Index];
				Result.AppendChar(Escaped == TEXT('n') ? TEXT('\n') : Escaped == TEXT('t') ? TEXT('\t') : Escaped);
				continue;
			}
			Result.AppendChar(Char);
		}
		return Result;
	}

	/** Value starts with a single quote; '' stands for one quote. */
	FString UnquoteSingle(const FString& Value)
	{
		FString Result;
		for (int32 Index = 1; Index < Value.Len(); ++Index)
		{
			const TCHAR Char = Value[Index];
			if (Char == TEXT('\''))
			{
				if (Index + 1 < Value.Len() && Value[Index + 1] == TEXT('\''))
				{
					Result.AppendChar(Char);
					++Index;
					continue;
				}
				break;
			}
			Result.AppendChar(Char);
		}
		return Result;
	}

	/** The value of a front matter field from the text after its colon and the indented lines that follow. */
	FString ReadScalar(const FString& FirstLine, const TArray<FString>& Continuation)
	{
		if (FirstLine.StartsWith(TEXT("|")) || FirstLine.StartsWith(TEXT(">")))
		{
			int32 Indent = 0;
			for (const FString& Line : Continuation)
			{
				if (!Line.TrimStartAndEnd().IsEmpty())
				{
					Indent = CountIndent(Line);
					break;
				}
			}

			TArray<FString> TextLines;
			for (const FString& Line : Continuation)
			{
				TextLines.Add(Line.TrimStartAndEnd().IsEmpty() ? FString() : Line.Mid(FMath::Min(Indent, CountIndent(Line))).TrimEnd());
			}
			if (FirstLine.StartsWith(TEXT("|")))
			{
				return FString::Join(TextLines, TEXT("\n")).TrimStartAndEnd();
			}

			// Folded: lines join with spaces and an empty line becomes a line break.
			FString Result;
			bool bAfterText = false;
			for (const FString& TextLine : TextLines)
			{
				if (TextLine.IsEmpty())
				{
					Result.AppendChar(TEXT('\n'));
					bAfterText = false;
					continue;
				}
				if (bAfterText)
				{
					Result.AppendChar(TEXT(' '));
				}
				Result += TextLine;
				bAfterText = true;
			}
			return Result.TrimStartAndEnd();
		}

		FString Joined = FirstLine;
		for (const FString& Line : Continuation)
		{
			const FString Trimmed = Line.TrimStartAndEnd();
			if (!Trimmed.IsEmpty())
			{
				Joined += Joined.IsEmpty() ? Trimmed : TEXT(" ") + Trimmed;
			}
		}
		if (Joined.StartsWith(TEXT("\"")))
		{
			return UnquoteDouble(Joined);
		}
		if (Joined.StartsWith(TEXT("'")))
		{
			return UnquoteSingle(Joined);
		}
		return StripComment(Joined).TrimStartAndEnd();
	}
}

namespace UE::AgentMcp
{
	bool ParseSkillFile(const FString& Text, TMap<FString, FString>& OutFields, FString& OutBody, FString& OutError)
	{
		using namespace SkillsPrivate;

		OutFields.Reset();
		OutBody.Reset();
		OutError.Reset();

		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, /*InCullEmpty=*/false);
		if (Lines.Num() > 0 && Lines[0].Len() > 0 && Lines[0][0] == 0xFEFF)
		{
			Lines[0].RightChopInline(1);
		}
		if (Lines.Num() == 0 || Lines[0].TrimEnd() != TEXT("---"))
		{
			OutError = TEXT("it does not start with a --- line that opens the front matter");
			return false;
		}

		int32 ClosingLine = INDEX_NONE;
		for (int32 Index = 1; Index < Lines.Num(); ++Index)
		{
			if (Lines[Index].TrimEnd() == TEXT("---"))
			{
				ClosingLine = Index;
				break;
			}
		}
		if (ClosingLine == INDEX_NONE)
		{
			OutError = TEXT("its front matter has no closing --- line");
			return false;
		}

		FString Key;
		FString FirstLine;
		TArray<FString> Continuation;
		auto FinishField = [&]()
		{
			if (!Key.IsEmpty())
			{
				OutFields.Add(Key, ReadScalar(FirstLine, Continuation));
			}
			Key.Reset();
			FirstLine.Reset();
			Continuation.Reset();
		};

		for (int32 Index = 1; Index < ClosingLine; ++Index)
		{
			const FString& Line = Lines[Index];
			const bool bTopLevel = Line.Len() > 0 && !FChar::IsWhitespace(Line[0]);
			if (bTopLevel && Line[0] == TEXT('#'))
			{
				continue;
			}
			int32 Colon = INDEX_NONE;
			if (bTopLevel && Line.FindChar(TEXT(':'), Colon))
			{
				FinishField();
				Key = Line.Left(Colon).TrimStartAndEnd();
				if (Key.Len() >= 2 && (Key[0] == TEXT('"') || Key[0] == TEXT('\'')) && Key[Key.Len() - 1] == Key[0])
				{
					Key = Key.Mid(1, Key.Len() - 2);
				}
				FirstLine = Line.Mid(Colon + 1).TrimStartAndEnd();
				continue;
			}
			if (!Key.IsEmpty())
			{
				Continuation.Add(Line);
			}
		}
		FinishField();

		int32 BodyStart = ClosingLine + 1;
		while (BodyStart < Lines.Num() && Lines[BodyStart].TrimStartAndEnd().IsEmpty())
		{
			++BodyStart;
		}
		TArray<FString> BodyLines;
		for (int32 Index = BodyStart; Index < Lines.Num(); ++Index)
		{
			BodyLines.Add(Lines[Index]);
		}
		OutBody = FString::Join(BodyLines, TEXT("\n")).TrimEnd();
		if (!OutBody.IsEmpty())
		{
			OutBody.AppendChar(TEXT('\n'));
		}
		return true;
	}

	FSkillScan ScanSkills()
	{
		using namespace SkillsPrivate;

		FSkillScan Scan;
		auto AddDirectory = [&Scan](const FString& Directory)
		{
			const bool bKnown = Scan.SearchedDirectories.ContainsByPredicate([&Directory](const FString& Existing)
			{
				return Existing.Equals(Directory, ESearchCase::IgnoreCase);
			});
			if (!Directory.IsEmpty() && !bKnown)
			{
				Scan.SearchedDirectories.Add(Directory);
			}
		};

		if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AgentMcp")))
		{
			AddDirectory(MakeFullDirectory(Plugin->GetBaseDir() / TEXT("Skills")));
		}
		const FString ProjectDirectory = MakeFullDirectory(FPaths::ProjectDir());
		AddDirectory(ProjectDirectory / TEXT("AgentMcp/Skills"));
		for (const FString& Entry : GetDefault<UAgentMcpSettings>()->SkillDirectories)
		{
			const FString Trimmed = Entry.TrimStartAndEnd();
			if (!Trimmed.IsEmpty())
			{
				AddDirectory(MakeFullDirectory(FPaths::IsRelative(Trimmed) ? ProjectDirectory / Trimmed : Trimmed));
			}
		}

		TMap<FString, FSkillInfo> SkillsByName;
		for (const FString& Directory : Scan.SearchedDirectories)
		{
			TArray<FString> Folders;
			IFileManager::Get().FindFiles(Folders, *(Directory / TEXT("*")), /*Files=*/false, /*Directories=*/true);
			Folders.Sort();
			for (const FString& Folder : Folders)
			{
				const FString SkillDirectory = Directory / Folder;
				const FString SkillFile = SkillDirectory / TEXT("SKILL.md");
				if (Folder.StartsWith(TEXT(".")) || !IFileManager::Get().FileExists(*SkillFile))
				{
					continue;
				}

				FString Text;
				TMap<FString, FString> Fields;
				FString Body;
				FString Error;
				if (!FFileHelper::LoadFileToString(Text, *SkillFile))
				{
					Error = TEXT("the file cannot be read");
				}
				else
				{
					ParseSkillFile(Text, Fields, Body, Error);
				}

				const FString Name = Fields.FindRef(TEXT("name"));
				const FString Description = Fields.FindRef(TEXT("description")).Replace(TEXT("\n"), TEXT(" "));
				if (Error.IsEmpty())
				{
					if (Name.IsEmpty())
					{
						Error = TEXT("its front matter has no name");
					}
					else if (!IsValidSkillName(Name))
					{
						Error = FString::Printf(TEXT("its name '%s' is not 1-%d lowercase letters, digits and single hyphens"), *Name, MaxNameLength);
					}
					else if (!Name.Equals(Folder, ESearchCase::CaseSensitive))
					{
						Error = FString::Printf(TEXT("its name '%s' differs from the name of its folder"), *Name);
					}
					else if (Description.IsEmpty())
					{
						Error = TEXT("its front matter has no description");
					}
					else if (Description.Len() > MaxDescriptionLength)
					{
						Error = FString::Printf(TEXT("its description has %d characters, more than %d"), Description.Len(), MaxDescriptionLength);
					}
				}
				if (!Error.IsEmpty())
				{
					Scan.Problems.Add(FString::Printf(TEXT("%s was skipped: %s."), *SkillFile, *Error));
					continue;
				}

				FSkillInfo Skill;
				Skill.Name = Name;
				Skill.Description = Description;
				Skill.Directory = SkillDirectory;
				if (const FSkillInfo* Replaced = SkillsByName.Find(Name))
				{
					Skill.OverriddenDirectory = Replaced->Directory;
				}
				SkillsByName.Add(Name, MoveTemp(Skill));
			}
		}

		SkillsByName.GenerateValueArray(Scan.Skills);
		Scan.Skills.Sort([](const FSkillInfo& A, const FSkillInfo& B) { return A.Name < B.Name; });
		return Scan;
	}

	FString BuildSkillInstructions()
	{
		const FSkillScan Scan = ScanSkills();
		if (Scan.Skills.IsEmpty())
		{
			return FString();
		}

		FString Entries;
		TArray<FString> Names;
		for (const FSkillInfo& Skill : Scan.Skills)
		{
			Entries += FString::Printf(TEXT("\n  - %s: %s"), *Skill.Name, *Skill.Description);
			Names.Add(Skill.Name);
		}
		if (Entries.Len() <= SkillsPrivate::MaxInstructionsCharacters)
		{
			return TEXT("- Skills are task guides served by this editor. Before a task that matches one, read it with skills_get:") + Entries;
		}
		return FString::Printf(TEXT("- Skills are task guides served by this editor: %s. skills_list says when to use each; read the matching one with skills_get before the task."),
			*FString::Join(Names, TEXT(", ")));
	}
}
