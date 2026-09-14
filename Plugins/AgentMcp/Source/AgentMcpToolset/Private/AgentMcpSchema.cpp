#include "AgentMcpSchema.h"

#include "GameFramework/Actor.h"
#include "JsonObjectConverter.h"
#include "JsonObjectWrapper.h"
#include "UObject/Class.h"
#include "UObject/EnumProperty.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"

namespace UE::AgentMcp::Schema
{
	namespace SchemaPrivate
	{
		constexpr int32 MaxStructDepth = 6;

		struct FFunctionDocs
		{
			FString Summary;
			FString Returns;
			TMap<FString, FString> Parameters;
		};

		FFunctionDocs ParseFunctionDocs(const UFunction* Function)
		{
			enum class ESection : uint8 { Summary, Parameter, Return, Other };

			FFunctionDocs Docs;
			TArray<FString> Lines;
			Function->GetMetaData(TEXT("ToolTip")).ParseIntoArrayLines(Lines, /*bCullEmpty=*/false);

			TArray<FString> SummaryLines;
			ESection Section = ESection::Summary;
			FString CurrentParameter;

			for (const FString& RawLine : Lines)
			{
				const FString Line = RawLine.TrimStartAndEnd();
				if (Line.StartsWith(TEXT("@param")))
				{
					const FString Rest = Line.RightChop(6).TrimStart();
					FString Text;
					if (!Rest.Split(TEXT(" "), &CurrentParameter, &Text))
					{
						CurrentParameter = Rest;
					}
					Docs.Parameters.Add(CurrentParameter, Text.TrimStartAndEnd());
					Section = ESection::Parameter;
				}
				else if (Line.StartsWith(TEXT("@return")))
				{
					Docs.Returns = Line.RightChop(7).TrimStartAndEnd();
					Section = ESection::Return;
				}
				else if (Line.StartsWith(TEXT("@")))
				{
					Section = ESection::Other;
				}
				else if (Section == ESection::Summary)
				{
					SummaryLines.Add(Line);
				}
				else if (!Line.IsEmpty() && Section == ESection::Parameter)
				{
					FString& ParameterText = Docs.Parameters.FindOrAdd(CurrentParameter);
					ParameterText += TEXT(" ") + Line;
				}
				else if (!Line.IsEmpty() && Section == ESection::Return)
				{
					Docs.Returns += TEXT(" ") + Line;
				}
			}

			Docs.Summary = FString::Join(SummaryLines, TEXT("\n")).TrimStartAndEnd();
			return Docs;
		}

		void SetDescription(const TSharedRef<FJsonObject>& Schema, const FString& Description)
		{
			if (!Description.IsEmpty())
			{
				Schema->SetStringField(TEXT("description"), Description);
			}
		}

		const UEnum* GetPropertyEnum(const FProperty* Property)
		{
			if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
			{
				return EnumProperty->GetEnum();
			}
			if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
			{
				return ByteProperty->Enum.Get();
			}
			return nullptr;
		}

		void AddEnumValues(const TSharedRef<FJsonObject>& Schema, const UEnum* Enum)
		{
			Schema->SetStringField(TEXT("type"), TEXT("string"));
			if (!Enum)
			{
				return;
			}

			TArray<TSharedPtr<FJsonValue>> Values;
			const int32 Count = Enum->ContainsExistingMax() ? Enum->NumEnums() - 1 : Enum->NumEnums();
			for (int32 Index = 0; Index < Count; ++Index)
			{
				if (!Enum->HasMetaData(TEXT("Hidden"), Index))
				{
					Values.Add(MakeShared<FJsonValueString>(Enum->GetNameStringByIndex(Index)));
				}
			}
			Schema->SetArrayField(TEXT("enum"), Values);
		}

		void AddDefault(const TSharedRef<FJsonObject>& Schema, const FProperty* Parameter, const FString& DefaultText)
		{
			if (CastField<FBoolProperty>(Parameter))
			{
				Schema->SetBoolField(TEXT("default"), DefaultText.ToBool());
			}
			else if (const UEnum* Enum = GetPropertyEnum(Parameter))
			{
				// UHT records enum defaults with or without the enum prefix; the schema lists short value names.
				FString ValueName = DefaultText;
				const int32 SeparatorIndex = ValueName.Find(TEXT("::"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
				if (SeparatorIndex != INDEX_NONE)
				{
					ValueName.RightChopInline(SeparatorIndex + 2);
				}
				if (Enum->GetIndexByNameString(ValueName) != INDEX_NONE)
				{
					Schema->SetStringField(TEXT("default"), ValueName);
				}
			}
			else if (CastField<FNumericProperty>(Parameter))
			{
				if (DefaultText.IsNumeric())
				{
					Schema->SetNumberField(TEXT("default"), FCString::Atod(*DefaultText));
				}
			}
			else if (CastField<FStrProperty>(Parameter) || CastField<FNameProperty>(Parameter) || CastField<FTextProperty>(Parameter))
			{
				Schema->SetStringField(TEXT("default"), DefaultText);
			}
		}

		FString ClassPathDescription(const UClass* MetaClass)
		{
			return FString::Printf(TEXT("Class name or path deriving from %s (for example StaticMeshActor, /Script/Engine.Actor or /Game/Folder/BP_Name.BP_Name_C)."), *GetNameSafe(MetaClass));
		}
	}

	FString GetJsonName(const FProperty* Property)
	{
		return FJsonObjectConverter::StandardizeCase(Property->GetName());
	}

	bool IsOutputParameter(const FProperty* Property)
	{
		return Property->HasAnyPropertyFlags(CPF_ReturnParm)
			|| (Property->HasAnyPropertyFlags(CPF_OutParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm));
	}

	bool IsInputParameter(const FProperty* Property)
	{
		return Property->HasAnyPropertyFlags(CPF_Parm) && !IsOutputParameter(Property);
	}

	bool IsSupportedProperty(const FProperty* Property, FString& OutReason)
	{
		if (CastField<FDelegateProperty>(Property) || CastField<FMulticastDelegateProperty>(Property) || CastField<FInterfaceProperty>(Property))
		{
			OutReason = FString::Printf(TEXT("'%s' has unsupported type %s"), *Property->GetName(), *Property->GetClass()->GetName());
			return false;
		}
		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			return IsSupportedProperty(ArrayProperty->Inner, OutReason);
		}
		if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			return IsSupportedProperty(SetProperty->ElementProp, OutReason);
		}
		if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			const FProperty* KeyProperty = MapProperty->KeyProp;
			const bool bKeyIsText = CastField<FStrProperty>(KeyProperty) || CastField<FNameProperty>(KeyProperty)
				|| CastField<FEnumProperty>(KeyProperty) || CastField<FNumericProperty>(KeyProperty);
			if (!bKeyIsText)
			{
				OutReason = FString::Printf(TEXT("'%s' is a map whose key cannot be a JSON object key"), *Property->GetName());
				return false;
			}
			return IsSupportedProperty(MapProperty->ValueProp, OutReason);
		}
		return true;
	}

	TSharedRef<FJsonObject> MakeStructSchema(const UStruct* Struct, int32 Depth)
	{
		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));
		if (!Struct)
		{
			return Schema;
		}

		if (Depth >= SchemaPrivate::MaxStructDepth)
		{
			SchemaPrivate::SetDescription(Schema, FString::Printf(TEXT("%s (nested too deeply to describe)."), *Struct->GetName()));
			return Schema;
		}

		TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (!It->HasAnyPropertyFlags(CPF_Deprecated))
			{
				Properties->SetObjectField(GetJsonName(*It), MakePropertySchema(*It, Depth + 1));
			}
		}
		Schema->SetObjectField(TEXT("properties"), Properties);
		SchemaPrivate::SetDescription(Schema, Struct->GetMetaData(TEXT("ToolTip")));
		return Schema;
	}

	TSharedRef<FJsonObject> MakePropertySchema(const FProperty* Property, int32 Depth)
	{
		using namespace SchemaPrivate;

		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();

		if (CastField<FBoolProperty>(Property))
		{
			Schema->SetStringField(TEXT("type"), TEXT("boolean"));
		}
		else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			AddEnumValues(Schema, EnumProperty->GetEnum());
		}
		else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property); ByteProperty && ByteProperty->Enum)
		{
			AddEnumValues(Schema, ByteProperty->Enum.Get());
		}
		else if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			Schema->SetStringField(TEXT("type"), NumericProperty->IsFloatingPoint() ? TEXT("number") : TEXT("integer"));
		}
		else if (CastField<FStrProperty>(Property) || CastField<FNameProperty>(Property) || CastField<FTextProperty>(Property))
		{
			Schema->SetStringField(TEXT("type"), TEXT("string"));
		}
		else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			const UScriptStruct* Struct = StructProperty->Struct.Get();
			const UScriptStruct::ICppStructOps* StructOps = Struct ? Struct->GetCppStructOps() : nullptr;
			if (Struct == FJsonObjectWrapper::StaticStruct())
			{
				Schema->SetStringField(TEXT("type"), TEXT("object"));
			}
			else if (StructOps && StructOps->HasExportTextItem())
			{
				// FJsonObjectConverter exchanges structs with custom text export as strings.
				Schema->SetStringField(TEXT("type"), TEXT("string"));
				SetDescription(Schema, FString::Printf(TEXT("%s in Unreal text form."), *Struct->GetName()));
			}
			else
			{
				Schema = MakeStructSchema(Struct, Depth);
			}
		}
		else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			Schema->SetStringField(TEXT("type"), TEXT("array"));
			Schema->SetObjectField(TEXT("items"), MakePropertySchema(ArrayProperty->Inner, Depth + 1));
		}
		else if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			Schema->SetStringField(TEXT("type"), TEXT("array"));
			Schema->SetBoolField(TEXT("uniqueItems"), true);
			Schema->SetObjectField(TEXT("items"), MakePropertySchema(SetProperty->ElementProp, Depth + 1));
		}
		else if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			Schema->SetStringField(TEXT("type"), TEXT("object"));
			Schema->SetObjectField(TEXT("additionalProperties"), MakePropertySchema(MapProperty->ValueProp, Depth + 1));
		}
		else if (const FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
		{
			Schema->SetStringField(TEXT("type"), TEXT("string"));
			SetDescription(Schema, ClassPathDescription(ClassProperty->MetaClass.Get()));
		}
		else if (const FSoftClassProperty* SoftClassProperty = CastField<FSoftClassProperty>(Property))
		{
			Schema->SetStringField(TEXT("type"), TEXT("string"));
			SetDescription(Schema, ClassPathDescription(SoftClassProperty->MetaClass.Get()));
		}
		else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			const UClass* PropertyClass = ObjectProperty->PropertyClass.Get();
			FString Description = FString::Printf(TEXT("Object path of a %s (for example /Game/Folder/Asset.Asset)."), *GetNameSafe(PropertyClass));
			if (PropertyClass && (PropertyClass->IsChildOf(AActor::StaticClass()) || AActor::StaticClass()->IsChildOf(PropertyClass)))
			{
				Description += TEXT(" For level objects an actor label or name is also accepted (editor level first, then the play session), and ActorLabel.ComponentName for components.");
			}
			Schema->SetStringField(TEXT("type"), TEXT("string"));
			SetDescription(Schema, Description);
		}
		else
		{
			SetDescription(Schema, FString::Printf(TEXT("Unsupported property type %s."), *Property->GetClass()->GetName()));
		}

		if (!Schema->HasField(TEXT("description")))
		{
			SetDescription(Schema, Property->GetMetaData(TEXT("ToolTip")));
		}
		return Schema;
	}

	bool TryGetParameterDefault(const UFunction* Function, const FProperty* Parameter, FString& OutDefault)
	{
		const FName Key(*FString::Printf(TEXT("CPP_Default_%s"), *Parameter->GetName()));
		if (!Function->HasMetaData(Key))
		{
			return false;
		}
		OutDefault = Function->GetMetaData(Key);
		return true;
	}

	bool IsAutoCreateRefTerm(const UFunction* Function, const FProperty* Parameter)
	{
		static const FName AutoCreateRefTermKey(TEXT("AutoCreateRefTerm"));
		if (!Function->HasMetaData(AutoCreateRefTermKey))
		{
			return false;
		}

		TArray<FString> ParameterNames;
		Function->GetMetaData(AutoCreateRefTermKey).ParseIntoArray(ParameterNames, TEXT(","), /*bCullEmpty=*/true);
		const FString ParameterName = Parameter->GetName();
		for (const FString& ListedName : ParameterNames)
		{
			if (ListedName.TrimStartAndEnd().Equals(ParameterName, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	TSharedRef<FJsonObject> MakeInputSchema(const UFunction* Function)
	{
		const SchemaPrivate::FFunctionDocs Docs = SchemaPrivate::ParseFunctionDocs(Function);

		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));

		TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Required;

		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			const FProperty* Parameter = *It;
			if (!IsInputParameter(Parameter))
			{
				continue;
			}

			const FString JsonName = GetJsonName(Parameter);
			TSharedRef<FJsonObject> ParameterSchema = MakePropertySchema(Parameter, 0);

			if (const FString* ParameterDoc = Docs.Parameters.Find(Parameter->GetName()); ParameterDoc && !ParameterDoc->IsEmpty())
			{
				const FString TypeHint = ParameterSchema->HasField(TEXT("description"))
					? TEXT(" ") + ParameterSchema->GetStringField(TEXT("description"))
					: FString();
				ParameterSchema->SetStringField(TEXT("description"), *ParameterDoc + TypeHint);
			}

			FString DefaultText;
			if (TryGetParameterDefault(Function, Parameter, DefaultText))
			{
				SchemaPrivate::AddDefault(ParameterSchema, Parameter, DefaultText);
			}
			else if (!IsAutoCreateRefTerm(Function, Parameter))
			{
				Required.Add(MakeShared<FJsonValueString>(JsonName));
			}

			Properties->SetObjectField(JsonName, ParameterSchema);
		}

		Schema->SetObjectField(TEXT("properties"), Properties);
		if (Required.Num() > 0)
		{
			Schema->SetArrayField(TEXT("required"), Required);
		}
		Schema->SetBoolField(TEXT("additionalProperties"), false);
		return Schema;
	}

	FString MakeToolDescription(const UFunction* Function)
	{
		const SchemaPrivate::FFunctionDocs Docs = SchemaPrivate::ParseFunctionDocs(Function);
		FString Description = Docs.Summary;
		if (!Docs.Returns.IsEmpty())
		{
			Description += TEXT("\nReturns: ") + Docs.Returns;
		}
		return Description.TrimStartAndEnd();
	}
}
