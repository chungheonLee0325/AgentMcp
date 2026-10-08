#include "AgentMcpSourceTools.h"

#include "AgentMcpSettings.h"

#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/WindowsHWrapper.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace UE::AgentMcp::SourceToolsPrivate
{
	/** Larger files are skipped by search and refused by read; they are generated or third-party data, not code to read. */
	constexpr int64 MaxFileBytes = 4 * 1024 * 1024;
	/** Search stops after this many files so one call cannot hold the game thread for long. */
	constexpr int32 MaxFilesSearched = 20000;
	constexpr int32 MaxMatchChars = 300;

	const TSet<FString>& GetTextExtensions()
	{
		static const TSet<FString> Extensions = {
			TEXT("h"), TEXT("hpp"), TEXT("inl"), TEXT("cpp"), TEXT("c"), TEXT("cc"), TEXT("cs"), TEXT("ini"), TEXT("json"), TEXT("txt"),
			TEXT("md"), TEXT("py"), TEXT("usf"), TEXT("ush"), TEXT("uplugin"), TEXT("uproject"), TEXT("xml"), TEXT("csv"), TEXT("yaml"),
			TEXT("yml"), TEXT("toml"), TEXT("proto"), TEXT("lua"), TEXT("js"), TEXT("ts"), TEXT("bat"), TEXT("ps1"), TEXT("sh"),
		};
		return Extensions;
	}

	/** Build output and caches inside the readable folders, which are never source. */
	bool IsInSkippedFolder(const FString& RelativePath)
	{
		static const TCHAR* const Skipped[] = { TEXT("/Binaries/"), TEXT("/Intermediate/"), TEXT("/Saved/"), TEXT("/DerivedDataCache/"),
			TEXT("/.git/"), TEXT("/.vs/"), TEXT("/node_modules/") };
		const FString Wrapped = TEXT("/") + RelativePath;
		for (const TCHAR* Folder : Skipped)
		{
			if (Wrapped.Contains(Folder))
			{
				return true;
			}
		}
		return false;
	}

	bool IsTextFile(const FString& Path)
	{
		return GetTextExtensions().Contains(FPaths::GetExtension(Path).ToLower());
	}

	FString GetProjectDir()
	{
		FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		FPaths::NormalizeDirectoryName(ProjectDir);
		return ProjectDir;
	}

	/** Absolute readable folders that exist, without a trailing slash. */
	TArray<FString> GetRoots()
	{
		const FString ProjectDir = GetProjectDir();
		TArray<FString> Roots;
		for (const FString& Folder : GetDefault<UAgentMcpSettings>()->SourceFolders)
		{
			const FString Trimmed = Folder.TrimStartAndEnd();
			if (Trimmed.IsEmpty())
			{
				continue;
			}
			FString Root = FPaths::IsRelative(Trimmed) ? FPaths::Combine(ProjectDir, Trimmed) : Trimmed;
			Root = FPaths::ConvertRelativePathToFull(Root);
			FPaths::NormalizeDirectoryName(Root);
			FPaths::CollapseRelativeDirectories(Root);
			if (FPaths::DirectoryExists(Root))
			{
				Roots.AddUnique(Root);
			}
		}
		return Roots;
	}

	FString ToRelative(const FString& AbsolutePath)
	{
		FString Relative = AbsolutePath;
		FPaths::MakePathRelativeTo(Relative, *(GetProjectDir() + TEXT("/")));
		return Relative;
	}

	TArray<FString> GetRelativeRoots(const TArray<FString>& Roots)
	{
		TArray<FString> Relative;
		for (const FString& Root : Roots)
		{
			Relative.Add(ToRelative(Root));
		}
		return Relative;
	}

	bool MatchesPattern(const FString& RelativePath, const FString& Pattern)
	{
		if (Pattern.IsEmpty())
		{
			return true;
		}
		// A pattern with a slash is matched against the path, otherwise against the file name.
		return Pattern.Contains(TEXT("/")) ? RelativePath.MatchesWildcard(Pattern) : FPaths::GetCleanFilename(RelativePath).MatchesWildcard(Pattern);
	}

	/** Text files under the roots, relative to the project folder, sorted. */
	TArray<FString> ListTextFiles(const TArray<FString>& Roots)
	{
		TArray<FString> Files;
		for (const FString& Root : Roots)
		{
			TArray<FString> Found;
			IFileManager::Get().FindFilesRecursive(Found, *Root, TEXT("*"), /*Files=*/true, /*Directories=*/false);
			for (const FString& File : Found)
			{
				const FString Relative = ToRelative(File);
				if (IsTextFile(File) && !IsInSkippedFolder(Relative))
				{
					Files.Add(Relative);
				}
			}
		}
		Files.Sort();
		return Files;
	}

	bool IsValidUtf8(const uint8* Data, int64 Size)
	{
		int64 Index = 0;
		while (Index < Size)
		{
			const uint8 Lead = Data[Index];
			int32 Continuation = 0;
			if (Lead < 0x80)
			{
				++Index;
				continue;
			}
			else if ((Lead & 0xE0) == 0xC0 && Lead >= 0xC2)
			{
				Continuation = 1;
			}
			else if ((Lead & 0xF0) == 0xE0)
			{
				Continuation = 2;
			}
			else if ((Lead & 0xF8) == 0xF0 && Lead <= 0xF4)
			{
				Continuation = 3;
			}
			else
			{
				return false;
			}
			if (Index + Continuation >= Size)
			{
				return false;
			}
			for (int32 Offset = 1; Offset <= Continuation; ++Offset)
			{
				if ((Data[Index + Offset] & 0xC0) != 0x80)
				{
					return false;
				}
			}
			Index += Continuation + 1;
		}
		return true;
	}

	/** Decodes a text file: BOMs first, then UTF-8 when the bytes are valid UTF-8, then CP949 (Korean Windows ANSI). */
	FString DecodeText(const TArray<uint8>& Bytes, FString& OutEncoding)
	{
		const uint8* Data = Bytes.GetData();
		const int64 Size = Bytes.Num();
		FString Text;
		if (Size >= 2 && ((Data[0] == 0xFF && Data[1] == 0xFE) || (Data[0] == 0xFE && Data[1] == 0xFF)))
		{
			OutEncoding = TEXT("utf-16");
			FFileHelper::BufferToString(Text, Data, Size);
			return Text;
		}
		if (Size >= 3 && Data[0] == 0xEF && Data[1] == 0xBB && Data[2] == 0xBF)
		{
			OutEncoding = TEXT("utf-8-bom");
			const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Data + 3), static_cast<int32>(Size - 3));
			return FString::ConstructFromPtrSize(Converted.Get(), Converted.Length());
		}
		if (IsValidUtf8(Data, Size))
		{
			OutEncoding = TEXT("utf-8");
			const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Data), static_cast<int32>(Size));
			return FString::ConstructFromPtrSize(Converted.Get(), Converted.Length());
		}
#if PLATFORM_WINDOWS
		constexpr UINT CodePageKorean = 949;
		const int32 Length = ::MultiByteToWideChar(CodePageKorean, 0, reinterpret_cast<const char*>(Data), static_cast<int>(Size), nullptr, 0);
		if (Length > 0)
		{
			TArray<TCHAR> Buffer;
			Buffer.SetNumUninitialized(Length);
			::MultiByteToWideChar(CodePageKorean, 0, reinterpret_cast<const char*>(Data), static_cast<int>(Size), Buffer.GetData(), Length);
			OutEncoding = TEXT("cp949");
			return FString::ConstructFromPtrSize(Buffer.GetData(), Length);
		}
#endif
		OutEncoding = TEXT("latin-1");
		FFileHelper::BufferToString(Text, Data, Size);
		return Text;
	}

	/** Encodes text the way DecodeText named it, so a CP949 file stays CP949 and a BOM stays. */
	bool EncodeText(const FString& Text, const FString& Encoding, TArray<uint8>& OutBytes)
	{
		OutBytes.Reset();
		if (Encoding == TEXT("utf-16"))
		{
			OutBytes.Add(0xFF);
			OutBytes.Add(0xFE);
			for (const TCHAR Character : Text)
			{
				const uint16 Unit = static_cast<uint16>(Character);
				OutBytes.Add(static_cast<uint8>(Unit & 0xFF));
				OutBytes.Add(static_cast<uint8>(Unit >> 8));
			}
			return true;
		}
#if PLATFORM_WINDOWS
		if (Encoding == TEXT("cp949"))
		{
			constexpr UINT CodePageKorean = 949;
			BOOL bUsedDefault = 0;
			const int32 Length = ::WideCharToMultiByte(CodePageKorean, 0, *Text, Text.Len(), nullptr, 0, nullptr, &bUsedDefault);
			if (Length < 0 || bUsedDefault)
			{
				// A character CP949 cannot hold; writing it would silently turn it into '?'.
				return false;
			}
			OutBytes.SetNumUninitialized(Length);
			::WideCharToMultiByte(CodePageKorean, 0, *Text, Text.Len(), reinterpret_cast<char*>(OutBytes.GetData()), Length, nullptr, nullptr);
			return true;
		}
#endif
		if (Encoding == TEXT("utf-8-bom"))
		{
			OutBytes.Append({ 0xEF, 0xBB, 0xBF });
		}
		const FTCHARToUTF8 Converted(*Text, Text.Len());
		OutBytes.Append(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
		return true;
	}

	/** Writes Text (with \n line endings) in the given encoding, turning line endings back into \r\n when bCrLf. */
	bool SaveText(const FString& AbsolutePath, const FString& Text, const FString& Encoding, bool bCrLf)
	{
		const FString Output = bCrLf ? Text.Replace(TEXT("\n"), TEXT("\r\n")) : Text;
		TArray<uint8> Bytes;
		if (!EncodeText(Output, Encoding, Bytes))
		{
			RaiseToolError(TEXT("NOT_SUPPORTED"), FString::Printf(TEXT("The new text has characters that %s cannot hold."), *Encoding),
				TEXT("Keep to characters the file's encoding supports."));
			return false;
		}
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(AbsolutePath), /*Tree=*/true);
		if (!FFileHelper::SaveArrayToFile(Bytes, *AbsolutePath))
		{
			RaiseToolError(TEXT("NOT_AVAILABLE"), FString::Printf(TEXT("'%s' could not be written."), *AbsolutePath),
				TEXT("The file may be read-only (checked in to source control) or open in another program."));
			return false;
		}
		return true;
	}

	bool LoadText(const FString& AbsolutePath, FString& OutText, FString& OutEncoding)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *AbsolutePath))
		{
			return false;
		}
		OutText = DecodeText(Bytes, OutEncoding);
		return true;
	}

	TArray<FString> SplitLines(const FString& Text)
	{
		TArray<FString> Lines;
		Text.Replace(TEXT("\r\n"), TEXT("\n")).ParseIntoArray(Lines, TEXT("\n"), /*InCullEmpty=*/false);
		return Lines;
	}

	/** Resolves a path given to source_read to an absolute path inside a readable folder, or raises a tool error. */
	bool ResolveReadablePath(const FString& Path, FString& OutAbsolute, bool bMustExist = true)
	{
		const FString Trimmed = Path.TrimStartAndEnd().TrimQuotes();
		if (Trimmed.IsEmpty())
		{
			RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("path is empty."));
			return false;
		}
		FString Absolute = FPaths::IsRelative(Trimmed) ? FPaths::Combine(GetProjectDir(), Trimmed) : Trimmed;
		Absolute = FPaths::ConvertRelativePathToFull(Absolute);
		FPaths::NormalizeFilename(Absolute);
		FPaths::CollapseRelativeDirectories(Absolute);

		const TArray<FString> Roots = GetRoots();
		const bool bInRoot = Roots.ContainsByPredicate([&Absolute](const FString& Root) { return FPaths::IsUnderDirectory(Absolute, Root); });
		if (!bInRoot || IsInSkippedFolder(ToRelative(Absolute)))
		{
			RaiseToolError(TEXT("NOT_AVAILABLE"), FString::Printf(TEXT("'%s' is outside the readable folders."), *Path),
				FString::Printf(TEXT("Readable folders: %s. Use source_find to locate files."), *FString::Join(GetRelativeRoots(Roots), TEXT(", "))));
			return false;
		}
		if (!IsTextFile(Absolute))
		{
			RaiseToolError(TEXT("NOT_SUPPORTED"), FString::Printf(TEXT("'%s' is not a text file the source tools read."), *Path),
				TEXT("Assets (.uasset, .umap) are read with the asset, blueprint and umg tools."));
			return false;
		}
		if (bMustExist && !FPaths::FileExists(Absolute))
		{
			RaiseToolError(TEXT("NOT_FOUND"), FString::Printf(TEXT("'%s' does not exist."), *Path), TEXT("Use source_find to locate files."));
			return false;
		}
		OutAbsolute = Absolute;
		return true;
	}
}

FAgentMcpSourceFindResult UAgentMcpSourceTools::Find(const FString& Pattern, int32 MaxResults)
{
	using namespace UE::AgentMcp::SourceToolsPrivate;

	FAgentMcpSourceFindResult Result;
	const TArray<FString> Roots = GetRoots();
	Result.Folders = GetRelativeRoots(Roots);
	const FString TrimmedPattern = Pattern.TrimStartAndEnd().Replace(TEXT("\\"), TEXT("/"));
	if (TrimmedPattern.IsEmpty())
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("pattern is empty."), TEXT("For example \"*Quest*.h\"."));
		return Result;
	}

	const int32 Limit = FMath::Clamp(MaxResults, 1, 1000);
	const FString ProjectDir = GetProjectDir();
	for (const FString& Relative : ListTextFiles(Roots))
	{
		if (!MatchesPattern(Relative, TrimmedPattern))
		{
			continue;
		}
		if (Result.Files.Num() >= Limit)
		{
			Result.bTruncated = true;
			break;
		}
		FAgentMcpSourceFile& File = Result.Files.AddDefaulted_GetRef();
		File.Path = Relative;
		File.SizeBytes = IFileManager::Get().FileSize(*FPaths::Combine(ProjectDir, Relative));
	}
	return Result;
}

FAgentMcpSourceSearchResult UAgentMcpSourceTools::Search(const FString& Query, const FString& FilePattern, bool bRegex, bool bCaseSensitive, int32 MaxResults)
{
	using namespace UE::AgentMcp::SourceToolsPrivate;

	FAgentMcpSourceSearchResult Result;
	if (Query.IsEmpty())
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("query is empty."));
		return Result;
	}

	TOptional<FRegexPattern> Regex;
	if (bRegex)
	{
		Regex.Emplace(Query, bCaseSensitive ? ERegexPatternFlags::None : ERegexPatternFlags::CaseInsensitive);
	}

	const int32 Limit = FMath::Clamp(MaxResults, 1, 500);
	const FString TrimmedPattern = FilePattern.TrimStartAndEnd().Replace(TEXT("\\"), TEXT("/"));
	const FString ProjectDir = GetProjectDir();
	const ESearchCase::Type SearchCase = bCaseSensitive ? ESearchCase::CaseSensitive : ESearchCase::IgnoreCase;

	for (const FString& Relative : ListTextFiles(GetRoots()))
	{
		if (!MatchesPattern(Relative, TrimmedPattern))
		{
			continue;
		}
		if (Result.FilesSearched >= MaxFilesSearched)
		{
			Result.bTruncated = true;
			break;
		}
		const FString Absolute = FPaths::Combine(ProjectDir, Relative);
		if (IFileManager::Get().FileSize(*Absolute) > MaxFileBytes)
		{
			continue;
		}

		FString Text;
		FString Encoding;
		if (!LoadText(Absolute, Text, Encoding))
		{
			continue;
		}
		++Result.FilesSearched;

		const TArray<FString> Lines = SplitLines(Text);
		for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
		{
			const FString& Line = Lines[LineIndex];
			bool bMatch = false;
			if (Regex.IsSet())
			{
				FRegexMatcher Matcher(Regex.GetValue(), Line);
				bMatch = Matcher.FindNext();
			}
			else
			{
				bMatch = Line.Contains(Query, SearchCase);
			}
			if (!bMatch)
			{
				continue;
			}
			if (Result.Matches.Num() >= Limit)
			{
				Result.bTruncated = true;
				return Result;
			}
			FAgentMcpSourceMatch& Match = Result.Matches.AddDefaulted_GetRef();
			Match.Path = Relative;
			Match.Line = LineIndex + 1;
			Match.Text = Line.TrimStartAndEnd().Left(MaxMatchChars);
		}
	}
	return Result;
}

FAgentMcpSourceReadResult UAgentMcpSourceTools::Read(const FString& Path, int32 StartLine, int32 LineCount)
{
	using namespace UE::AgentMcp::SourceToolsPrivate;

	FAgentMcpSourceReadResult Result;
	FString Absolute;
	if (!ResolveReadablePath(Path, Absolute))
	{
		return Result;
	}
	if (IFileManager::Get().FileSize(*Absolute) > MaxFileBytes)
	{
		UE::AgentMcp::RaiseToolError(TEXT("NOT_SUPPORTED"), FString::Printf(TEXT("'%s' is larger than 4 MB."), *Path),
			TEXT("Use source_search to find the lines you need."));
		return Result;
	}

	FString Text;
	if (!LoadText(Absolute, Text, Result.Encoding))
	{
		UE::AgentMcp::RaiseToolError(TEXT("NOT_AVAILABLE"), FString::Printf(TEXT("'%s' could not be read."), *Path));
		return Result;
	}

	const TArray<FString> Lines = SplitLines(Text);
	Result.Path = ToRelative(Absolute);
	Result.TotalLines = Lines.Num();
	Result.StartLine = FMath::Clamp(StartLine, 1, FMath::Max(1, Lines.Num()));
	const int32 Count = FMath::Clamp(LineCount, 1, 2000);
	Result.EndLine = FMath::Min(Lines.Num(), Result.StartLine + Count - 1);
	Result.bTruncated = Result.EndLine < Lines.Num();

	TStringBuilder<4096> Builder;
	for (int32 LineNumber = Result.StartLine; LineNumber <= Result.EndLine; ++LineNumber)
	{
		Builder.Appendf(TEXT("%6d  %s\n"), LineNumber, *Lines[LineNumber - 1]);
	}
	Result.Text = Builder.ToString();
	return Result;
}

FAgentMcpSourceEditResult UAgentMcpSourceTools::Replace(const FString& Path, const FString& OldText, const FString& NewText, bool bReplaceAll)
{
	using namespace UE::AgentMcp::SourceToolsPrivate;

	FAgentMcpSourceEditResult Result;
	FString Absolute;
	if (!ResolveReadablePath(Path, Absolute))
	{
		return Result;
	}
	if (OldText.IsEmpty())
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("oldText is empty."), TEXT("Use source_write to replace a whole file."));
		return Result;
	}

	FString Text;
	if (!LoadText(Absolute, Text, Result.Encoding))
	{
		UE::AgentMcp::RaiseToolError(TEXT("NOT_AVAILABLE"), FString::Printf(TEXT("'%s' could not be read."), *Path));
		return Result;
	}

	// Matching works on \n line endings, the form source_read shows; the file gets its own endings back on save.
	const bool bCrLf = Text.Contains(TEXT("\r\n"));
	Text.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
	const FString Old = OldText.Replace(TEXT("\r\n"), TEXT("\n"));
	const FString New = NewText.Replace(TEXT("\r\n"), TEXT("\n"));

	int32 Count = 0;
	for (int32 Index = Text.Find(Old, ESearchCase::CaseSensitive); Index != INDEX_NONE;
		Index = Text.Find(Old, ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + Old.Len()))
	{
		++Count;
	}
	if (Count == 0)
	{
		UE::AgentMcp::RaiseToolError(TEXT("NOT_FOUND"), TEXT("oldText does not occur in the file."),
			TEXT("Copy it exactly from source_read, without the line numbers, including spaces and tabs."));
		return Result;
	}
	if (Count > 1 && !bReplaceAll)
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("oldText occurs %d times."), Count),
			TEXT("Include more surrounding lines so it is unique, or set bReplaceAll."));
		return Result;
	}

	Text.ReplaceInline(*Old, *New, ESearchCase::CaseSensitive);
	if (!SaveText(Absolute, Text, Result.Encoding, bCrLf))
	{
		return Result;
	}
	Result.Path = ToRelative(Absolute);
	Result.Replacements = Count;
	Result.TotalLines = SplitLines(Text).Num();
	return Result;
}

FAgentMcpSourceEditResult UAgentMcpSourceTools::Write(const FString& Path, const FString& Content, bool bOverwrite)
{
	using namespace UE::AgentMcp::SourceToolsPrivate;

	FAgentMcpSourceEditResult Result;
	FString Absolute;
	if (!ResolveReadablePath(Path, Absolute, /*bMustExist=*/false))
	{
		return Result;
	}

	const FString Text = Content.Replace(TEXT("\r\n"), TEXT("\n"));
	bool bCrLf = false;
	Result.Encoding = TEXT("utf-8");
	if (FPaths::FileExists(Absolute))
	{
		if (!bOverwrite)
		{
			UE::AgentMcp::RaiseToolError(TEXT("ASSET_EXISTS"), FString::Printf(TEXT("'%s' already exists."), *Path),
				TEXT("Use source_replace for a change, or set bOverwrite to replace the whole file."));
			return Result;
		}
		FString Existing;
		if (LoadText(Absolute, Existing, Result.Encoding))
		{
			bCrLf = Existing.Contains(TEXT("\r\n"));
		}
	}
	else
	{
		Result.bCreated = true;
#if PLATFORM_WINDOWS
		bCrLf = true;
#endif
	}

	if (!SaveText(Absolute, Text, Result.Encoding, bCrLf))
	{
		return Result;
	}
	Result.Path = ToRelative(Absolute);
	Result.TotalLines = SplitLines(Text).Num();
	return Result;
}
