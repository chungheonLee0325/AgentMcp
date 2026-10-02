#include "AgentMcpClientConfig.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UE::AgentMcp::ClientConfig
{
	namespace Private
	{
		/** Saves Text when it differs from what is on disk, and records which of the three outcomes happened. */
		void SaveIfChanged(const FString& FilePath, const FString& Text, FWriteResult& Out)
		{
			FString Existing;
			if (FFileHelper::LoadFileToString(Existing, *FilePath) && Existing.Equals(Text, ESearchCase::CaseSensitive))
			{
				Out.Unchanged.Add(FilePath);
				return;
			}
			if (FFileHelper::SaveStringToFile(Text, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				Out.Written.Add(FilePath);
			}
			else
			{
				Out.Failures.Add(FilePath + TEXT(": the file could not be written"));
			}
		}

		FString MakeMcpJson(const FString& ExistingText, const FString& EndpointUrl, const FString& ServerName)
		{
			// The file may already name other servers, so it is merged rather than replaced.
			TSharedPtr<FJsonObject> Root;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ExistingText);
			if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
			{
				Root = MakeShared<FJsonObject>();
			}

			TSharedPtr<FJsonObject> Servers = Root->HasTypedField<EJson::Object>(TEXT("mcpServers"))
				? Root->GetObjectField(TEXT("mcpServers")) : MakeShared<FJsonObject>();

			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("type"), TEXT("http"));
			Entry->SetStringField(TEXT("url"), EndpointUrl);
			Servers->SetObjectField(ServerName, Entry);
			Root->SetObjectField(TEXT("mcpServers"), Servers);

			FString Text;
			const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text);
			FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
			return Text;
		}

		FString MakeCodexSection(const FString& EndpointUrl, const FString& AuthToken, const FString& ServerName)
		{
			// tool_timeout_sec raises the Codex default of 60 seconds, which compiling, saving and play sessions exceed.
			FString Section = FString::Printf(TEXT("[mcp_servers.%s]\nurl = \"%s\"\ntool_timeout_sec = 600\n"), *ServerName, *EndpointUrl);
			if (!AuthToken.IsEmpty())
			{
				Section += FString::Printf(TEXT("http_headers = { Authorization = \"Bearer %s\" }\n"), *AuthToken);
			}
			return Section;
		}

		/** Replaces the [mcp_servers.<name>] table of a TOML file, keeping every other line, or appends it. */
		FString MergeCodexSection(const FString& ExistingText, const FString& Section, const FString& ServerName)
		{
			const FString Header = FString::Printf(TEXT("[mcp_servers.%s]"), *ServerName);
			TArray<FString> Lines;
			ExistingText.ParseIntoArray(Lines, TEXT("\n"), /*InCullEmpty=*/false);

			int32 Start = INDEX_NONE;
			for (int32 Index = 0; Index < Lines.Num(); ++Index)
			{
				if (Lines[Index].TrimStartAndEnd().Equals(Header, ESearchCase::CaseSensitive))
				{
					Start = Index;
					break;
				}
			}
			if (Start == INDEX_NONE)
			{
				FString Text = ExistingText;
				if (!Text.IsEmpty())
				{
					if (!Text.EndsWith(TEXT("\n")))
					{
						Text += TEXT("\n");
					}
					Text += TEXT("\n");
				}
				return Text + Section;
			}

			// The table runs to the next table header, so only the keys of this entry are replaced and nothing else moves.
			int32 End = Start + 1;
			while (End < Lines.Num() && !Lines[End].TrimStart().StartsWith(TEXT("[")))
			{
				++End;
			}

			FString SectionBody = Section;
			SectionBody.RemoveFromEnd(TEXT("\n"));
			TArray<FString> SectionLines;
			SectionBody.ParseIntoArray(SectionLines, TEXT("\n"), /*InCullEmpty=*/false);
			if (End < Lines.Num())
			{
				// Keep the blank line that separated this entry from the next table, so a second write changes nothing.
				SectionLines.Add(FString());
			}

			TArray<FString> Merged;
			Merged.Append(MakeArrayView(Lines.GetData(), Start));
			Merged.Append(SectionLines);
			Merged.Append(MakeArrayView(Lines.GetData() + End, Lines.Num() - End));
			return FString::Join(Merged, TEXT("\n"));
		}
	}

	void WriteProjectFiles(const FString& EndpointUrl, const FString& AuthToken, const FString& ServerName, FWriteResult& Out)
	{
		using namespace Private;

		const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());

		const FString McpJsonPath = FPaths::Combine(ProjectDir, TEXT(".mcp.json"));
		const FString CodexPath = FPaths::Combine(ProjectDir, TEXT(".codex"), TEXT("config.toml"));
		// Project files are often committed, so they never carry the token: with one set, the entries are the user's to write.
		if (!AuthToken.IsEmpty())
		{
			Out.Failures.Add(McpJsonPath + TEXT(": not written, because AuthToken is set and a project file would carry it"));
			Out.Failures.Add(CodexPath + TEXT(": not written, because AuthToken is set and a project file would carry it"));
			return;
		}

		FString ExistingJson;
		FFileHelper::LoadFileToString(ExistingJson, *McpJsonPath);
		SaveIfChanged(McpJsonPath, MakeMcpJson(ExistingJson, EndpointUrl, ServerName), Out);

		FString ExistingToml;
		FFileHelper::LoadFileToString(ExistingToml, *CodexPath);
		SaveIfChanged(CodexPath, MergeCodexSection(ExistingToml, MakeCodexSection(EndpointUrl, AuthToken, ServerName), ServerName), Out);
	}

	void WriteUserCodexConfig(const FString& EndpointUrl, const FString& AuthToken, const FString& ServerName, FWriteResult& Out)
	{
		using namespace Private;

		FString Home = FPlatformProcess::UserHomeDir();
		if (Home.IsEmpty())
		{
			Home = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
		}
		if (Home.IsEmpty())
		{
			Out.Failures.Add(TEXT("~/.codex/config.toml: the home folder could not be found"));
			return;
		}

		const FString ConfigPath = FPaths::Combine(FPaths::ConvertRelativePathToFull(Home), TEXT(".codex"), TEXT("config.toml"));
		FString Existing;
		const bool bExists = FFileHelper::LoadFileToString(Existing, *ConfigPath);
		const FString Merged = MergeCodexSection(Existing, MakeCodexSection(EndpointUrl, AuthToken, ServerName), ServerName);
		if (bExists && Merged.Equals(Existing, ESearchCase::CaseSensitive))
		{
			Out.Unchanged.Add(ConfigPath);
			return;
		}

		// The file belongs to the user and holds their other settings, so the first change keeps a copy of what was there.
		const FString BackupPath = ConfigPath + TEXT(".agentmcp.bak");
		if (bExists && !IFileManager::Get().FileExists(*BackupPath))
		{
			IFileManager::Get().Copy(*BackupPath, *ConfigPath);
		}
		SaveIfChanged(ConfigPath, Merged, Out);
	}
}
