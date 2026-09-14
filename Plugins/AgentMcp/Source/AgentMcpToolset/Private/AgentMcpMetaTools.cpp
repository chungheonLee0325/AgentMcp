#include "AgentMcpMetaTools.h"

#include "AgentMcpReflectedTool.h"

namespace UE::AgentMcp::MetaToolsPrivate
{
	TSharedRef<FJsonObject> MakeObjectSchema(const TArray<TPair<FString, TSharedRef<FJsonObject>>>& Properties, const TArray<FString>& Required)
	{
		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));

		TSharedRef<FJsonObject> PropertyObject = MakeShared<FJsonObject>();
		for (const TPair<FString, TSharedRef<FJsonObject>>& Property : Properties)
		{
			PropertyObject->SetObjectField(Property.Key, Property.Value);
		}
		Schema->SetObjectField(TEXT("properties"), PropertyObject);

		if (Required.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> RequiredValues;
			for (const FString& Name : Required)
			{
				RequiredValues.Add(MakeShared<FJsonValueString>(Name));
			}
			Schema->SetArrayField(TEXT("required"), RequiredValues);
		}
		Schema->SetBoolField(TEXT("additionalProperties"), false);
		return Schema;
	}

	TSharedRef<FJsonObject> MakeTypedSchema(const TCHAR* Type, const FString& Description)
	{
		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), Type);
		Schema->SetStringField(TEXT("description"), Description);
		return Schema;
	}

	TSharedPtr<FJsonObject> MakeReadOnlyAnnotations()
	{
		TSharedRef<FJsonObject> Annotations = MakeShared<FJsonObject>();
		Annotations->SetBoolField(TEXT("readOnlyHint"), true);
		Annotations->SetBoolField(TEXT("openWorldHint"), false);
		return Annotations;
	}

	class FListToolsetsTool final : public IAgentMcpTool
	{
	public:
		explicit FListToolsetsTool(const TSharedRef<const FToolCatalog>& InCatalog) : Catalog(InCatalog) {}

		virtual FString GetName() const override { return TEXT("toolsets_list"); }
		virtual FString GetDescription() const override
		{
			return TEXT("Lists the available Unreal Editor toolsets with their descriptions and tool names. Call toolsets_describe for input schemas, then tools_call to run a tool.\n[Read-only]");
		}
		virtual TSharedRef<FJsonObject> GetInputSchema() const override { return MakeObjectSchema({}, {}); }
		virtual TSharedPtr<FJsonObject> GetAnnotations() const override { return MakeReadOnlyAnnotations(); }

		virtual void Run(const TSharedRef<FJsonObject>& Arguments, const FAgentMcpCallContext& Context, FAgentMcpToolCompletion&& OnComplete) override
		{
			TMap<FString, TArray<TSharedPtr<FJsonValue>>> ToolNamesByToolset;
			for (const TSharedRef<FReflectedTool>& Tool : Catalog->Tools)
			{
				ToolNamesByToolset.FindOrAdd(Tool->GetToolsetName()).Add(MakeShared<FJsonValueString>(Tool->GetName()));
			}

			TArray<TSharedPtr<FJsonValue>> Toolsets;
			for (const TPair<FString, TArray<TSharedPtr<FJsonValue>>>& Pair : ToolNamesByToolset)
			{
				TSharedRef<FJsonObject> Toolset = MakeShared<FJsonObject>();
				Toolset->SetStringField(TEXT("name"), Pair.Key);
				if (const FString* Description = Catalog->ToolsetDescriptions.Find(Pair.Key))
				{
					Toolset->SetStringField(TEXT("description"), *Description);
				}
				Toolset->SetArrayField(TEXT("tools"), Pair.Value);
				Toolsets.Add(MakeShared<FJsonValueObject>(Toolset));
			}

			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetArrayField(TEXT("toolsets"), Toolsets);
			OnComplete(FAgentMcpToolResult::MakeJson(Result));
		}

	private:
		TSharedRef<const FToolCatalog> Catalog;
	};

	class FDescribeToolsetTool final : public IAgentMcpTool
	{
	public:
		explicit FDescribeToolsetTool(const TSharedRef<const FToolCatalog>& InCatalog) : Catalog(InCatalog) {}

		virtual FString GetName() const override { return TEXT("toolsets_describe"); }
		virtual FString GetDescription() const override
		{
			return TEXT("Returns every tool of one toolset with its description, input schema and annotations.\n[Read-only]");
		}
		virtual TSharedRef<FJsonObject> GetInputSchema() const override
		{
			return MakeObjectSchema({ { TEXT("toolset"), MakeTypedSchema(TEXT("string"), TEXT("Toolset name from toolsets_list.")) } }, { TEXT("toolset") });
		}
		virtual TSharedPtr<FJsonObject> GetAnnotations() const override { return MakeReadOnlyAnnotations(); }

		virtual void Run(const TSharedRef<FJsonObject>& Arguments, const FAgentMcpCallContext& Context, FAgentMcpToolCompletion&& OnComplete) override
		{
			FString ToolsetName;
			if (!Arguments->TryGetStringField(TEXT("toolset"), ToolsetName) || ToolsetName.IsEmpty())
			{
				OnComplete(FAgentMcpToolResult::MakeError(TEXT("INVALID_ARGUMENT"), TEXT("Missing required argument 'toolset'.")));
				return;
			}

			TArray<TSharedPtr<FJsonValue>> Tools;
			for (const TSharedRef<FReflectedTool>& Tool : Catalog->Tools)
			{
				if (!Tool->GetToolsetName().Equals(ToolsetName, ESearchCase::IgnoreCase))
				{
					continue;
				}
				TSharedRef<FJsonObject> ToolObject = MakeShared<FJsonObject>();
				ToolObject->SetStringField(TEXT("name"), Tool->GetName());
				ToolObject->SetStringField(TEXT("description"), Tool->GetDescription());
				ToolObject->SetObjectField(TEXT("inputSchema"), Tool->GetInputSchema());
				ToolObject->SetObjectField(TEXT("annotations"), Tool->GetAnnotations());
				Tools.Add(MakeShared<FJsonValueObject>(ToolObject));
			}

			if (Tools.Num() == 0)
			{
				OnComplete(FAgentMcpToolResult::MakeError(TEXT("NOT_FOUND"), FString::Printf(TEXT("Unknown toolset '%s'."), *ToolsetName), TEXT("Call toolsets_list to see the available toolsets.")));
				return;
			}

			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("toolset"), ToolsetName);
			if (const FString* Description = Catalog->ToolsetDescriptions.Find(ToolsetName))
			{
				Result->SetStringField(TEXT("description"), *Description);
			}
			Result->SetArrayField(TEXT("tools"), Tools);
			OnComplete(FAgentMcpToolResult::MakeJson(Result));
		}

	private:
		TSharedRef<const FToolCatalog> Catalog;
	};

	class FCallToolTool final : public IAgentMcpTool
	{
	public:
		explicit FCallToolTool(const TSharedRef<const FToolCatalog>& InCatalog) : Catalog(InCatalog) {}

		virtual FString GetName() const override { return TEXT("tools_call"); }
		virtual FString GetDescription() const override
		{
			return TEXT("Runs a toolset tool by name with the given arguments. Use toolsets_describe to get the tool's input schema. The inner tool's access policy (undo transaction, PIE guard) still applies.");
		}
		virtual TSharedRef<FJsonObject> GetInputSchema() const override
		{
			TSharedRef<FJsonObject> ArgumentsSchema = MakeTypedSchema(TEXT("object"), TEXT("Arguments matching the tool's input schema."));
			return MakeObjectSchema(
				{
					{ TEXT("tool"), MakeTypedSchema(TEXT("string"), TEXT("Tool name, for example editor_get_state.")) },
					{ TEXT("arguments"), ArgumentsSchema },
				},
				{ TEXT("tool") });
		}

		virtual void Run(const TSharedRef<FJsonObject>& Arguments, const FAgentMcpCallContext& Context, FAgentMcpToolCompletion&& OnComplete) override
		{
			FString ToolName;
			if (!Arguments->TryGetStringField(TEXT("tool"), ToolName) || ToolName.IsEmpty())
			{
				OnComplete(FAgentMcpToolResult::MakeError(TEXT("INVALID_ARGUMENT"), TEXT("Missing required argument 'tool'.")));
				return;
			}

			TSharedRef<FJsonObject> InnerArguments = MakeShared<FJsonObject>();
			const TSharedPtr<FJsonObject>* ArgumentsObject = nullptr;
			if (Arguments->TryGetObjectField(TEXT("arguments"), ArgumentsObject) && ArgumentsObject && ArgumentsObject->IsValid())
			{
				InnerArguments = ArgumentsObject->ToSharedRef();
			}

			for (const TSharedRef<FReflectedTool>& Tool : Catalog->Tools)
			{
				if (Tool->GetName().Equals(ToolName, ESearchCase::IgnoreCase))
				{
					Tool->Run(InnerArguments, Context, MoveTemp(OnComplete));
					return;
				}
			}

			OnComplete(FAgentMcpToolResult::MakeError(TEXT("NOT_FOUND"), FString::Printf(TEXT("Unknown tool '%s'."), *ToolName), TEXT("Call toolsets_list to see the available tools.")));
		}

	private:
		TSharedRef<const FToolCatalog> Catalog;
	};
}

namespace UE::AgentMcp
{
	TArray<TSharedRef<IAgentMcpTool>> CreateToolSearchTools(const TSharedRef<const FToolCatalog>& Catalog)
	{
		using namespace MetaToolsPrivate;
		return
		{
			MakeShared<FListToolsetsTool>(Catalog),
			MakeShared<FDescribeToolsetTool>(Catalog),
			MakeShared<FCallToolTool>(Catalog),
		};
	}
}
