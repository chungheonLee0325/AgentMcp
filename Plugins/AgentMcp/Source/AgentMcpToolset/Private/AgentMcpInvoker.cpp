#include "AgentMcpInvoker.h"

#include "AgentMcpErrorScope.h"
#include "AgentMcpSchema.h"
#include "AgentMcpToolset.h"

#include "Components/ActorComponent.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "JsonObjectConverter.h"
#include "JsonObjectWrapper.h"
#include "Misc/ScopeExit.h"
#include "UObject/Class.h"
#include "UObject/EnumProperty.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace UE::AgentMcp::InvokerPrivate
{
	/** Longest text accepted where Unreal builds an FName from it; FName asserts on longer input. */
	constexpr int32 MaxNameLength = NAME_SIZE - 1;

	constexpr int32 MaxListedCandidates = 10;

	/** Accepts "Class'/Game/Path.Object'" (Unreal export text) as well as a plain path. */
	FString StripObjectPathWrapper(const FString& InText)
	{
		FString Path = InText.TrimStartAndEnd();
		int32 QuoteIndex = INDEX_NONE;
		if (Path.EndsWith(TEXT("'")) && Path.FindChar(TEXT('\''), QuoteIndex) && QuoteIndex < Path.Len() - 1)
		{
			Path = Path.Mid(QuoteIndex + 1, Path.Len() - QuoteIndex - 2);
		}
		return Path;
	}

	enum class EResolveResult : uint8
	{
		Found,
		NotFound,
		Ambiguous,
	};

	/**
	 * Finds an actor by object name (preferred) or label, in the editor world first and then in the play world.
	 * Several matches in one world are reported as candidates instead of silently picking one.
	 */
	EResolveResult FindActor(const FString& NameOrLabel, AActor*& OutActor, TArray<FString>& OutCandidates)
	{
		OutActor = nullptr;
		if (!GEditor)
		{
			return EResolveResult::NotFound;
		}

		UWorld* const Worlds[] = { GEditor->GetEditorWorldContext().World(), GEditor->PlayWorld.Get() };
		for (UWorld* World : Worlds)
		{
			if (!World)
			{
				continue;
			}

			TArray<AActor*> ByName;
			TArray<AActor*> ByLabel;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (!IsValid(Actor))
				{
					continue;
				}
				if (Actor->GetName().Equals(NameOrLabel, ESearchCase::IgnoreCase))
				{
					ByName.Add(Actor);
				}
				else if (Actor->GetActorLabel().Equals(NameOrLabel, ESearchCase::IgnoreCase))
				{
					ByLabel.Add(Actor);
				}
			}

			const TArray<AActor*>& Matches = ByName.Num() > 0 ? ByName : ByLabel;
			if (Matches.Num() == 1)
			{
				OutActor = Matches[0];
				return EResolveResult::Found;
			}
			if (Matches.Num() > 1)
			{
				for (const AActor* Match : Matches)
				{
					OutCandidates.Add(Match->GetPathName());
				}
				return EResolveResult::Ambiguous;
			}
		}
		return EResolveResult::NotFound;
	}

	/** Resolves an object path, or for level objects an actor label or name, or "ActorLabel.ComponentName". */
	EResolveResult ResolveObject(const FString& InText, const UClass* ExpectedClass, UObject*& OutObject, TArray<FString>& OutCandidates)
	{
		OutObject = nullptr;
		const FString Path = StripObjectPathWrapper(InText);
		if (Path.IsEmpty())
		{
			return EResolveResult::NotFound;
		}

		if (Path.StartsWith(TEXT("/")))
		{
			UObject* Object = StaticFindObject(UObject::StaticClass(), nullptr, *Path);
			const bool bHasObjectName = Path.Contains(TEXT("."));
			if (!bHasObjectName && (!Object || Object->GetOuter() == nullptr))
			{
				// "/Game/Folder/Asset" names the package (an object without an outer); callers almost always mean the asset inside it.
				int32 SlashIndex = INDEX_NONE;
				if (Path.FindLastChar(TEXT('/'), SlashIndex) && SlashIndex < Path.Len() - 1)
				{
					const FString AssetPath = FString::Printf(TEXT("%s.%s"), *Path, *Path.Mid(SlashIndex + 1));
					UObject* Asset = StaticFindObject(UObject::StaticClass(), nullptr, *AssetPath);
					if (!Asset)
					{
						Asset = FSoftObjectPath(AssetPath).TryLoad();
					}
					if (Asset)
					{
						Object = Asset;
					}
				}
			}
			else if (!Object)
			{
				Object = FSoftObjectPath(Path).TryLoad();
			}

			// A Blueprint class path (..._C) names the generated class; tools that take a Blueprint want the Blueprint asset.
			if (const UClass* ClassObject = Cast<UClass>(Object); ClassObject && ExpectedClass && ExpectedClass->IsChildOf(UBlueprint::StaticClass()) && ClassObject->ClassGeneratedBy)
			{
				Object = ClassObject->ClassGeneratedBy;
			}

			OutObject = IsValid(Object) ? Object : nullptr;
			return OutObject ? EResolveResult::Found : EResolveResult::NotFound;
		}

		const bool bAcceptsActor = !ExpectedClass || AActor::StaticClass()->IsChildOf(ExpectedClass) || ExpectedClass->IsChildOf(AActor::StaticClass());
		const bool bAcceptsComponent = !ExpectedClass || UActorComponent::StaticClass()->IsChildOf(ExpectedClass) || ExpectedClass->IsChildOf(UActorComponent::StaticClass());

		if (bAcceptsActor)
		{
			AActor* Actor = nullptr;
			const EResolveResult ActorResult = FindActor(Path, Actor, OutCandidates);
			if (ActorResult != EResolveResult::NotFound)
			{
				OutObject = Actor;
				return ActorResult;
			}
		}

		int32 DotIndex = INDEX_NONE;
		if (bAcceptsComponent && Path.FindLastChar(TEXT('.'), DotIndex) && DotIndex > 0 && DotIndex < Path.Len() - 1)
		{
			AActor* Owner = nullptr;
			const EResolveResult OwnerResult = FindActor(Path.Left(DotIndex), Owner, OutCandidates);
			if (OwnerResult == EResolveResult::Ambiguous)
			{
				return OwnerResult;
			}
			if (Owner)
			{
				const FString ComponentName = Path.Mid(DotIndex + 1);
				for (UActorComponent* Component : Owner->GetComponents())
				{
					if (IsValid(Component) && Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
					{
						OutObject = Component;
						return EResolveResult::Found;
					}
				}
			}
		}
		return EResolveResult::NotFound;
	}

	UClass* ResolveClass(const FString& InText)
	{
		const FString Path = StripObjectPathWrapper(InText);
		if (Path.IsEmpty())
		{
			return nullptr;
		}
		if (!Path.StartsWith(TEXT("/")))
		{
			return UClass::TryFindTypeSlow<UClass>(Path);
		}
		if (UClass* Found = FindObject<UClass>(nullptr, *Path))
		{
			return Found;
		}
		UObject* Loaded = FSoftObjectPath(Path).TryLoad();
		if (UClass* LoadedClass = Cast<UClass>(Loaded))
		{
			return LoadedClass;
		}
		if (const UBlueprint* Blueprint = Cast<UBlueprint>(Loaded))
		{
			return Blueprint->GeneratedClass;
		}
		return nullptr;
	}

	const TCHAR* JsonTypeName(EJson Type)
	{
		switch (Type)
		{
		case EJson::Null: return TEXT("null");
		case EJson::String: return TEXT("a string");
		case EJson::Number: return TEXT("a number");
		case EJson::Boolean: return TEXT("a boolean");
		case EJson::Array: return TEXT("an array");
		case EJson::Object: return TEXT("an object");
		default: return TEXT("missing");
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

	FString DescribeEnumValues(const UEnum* Enum)
	{
		TArray<FString> Names;
		const int32 Count = Enum->ContainsExistingMax() ? Enum->NumEnums() - 1 : Enum->NumEnums();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (!Enum->HasMetaData(TEXT("Hidden"), Index))
			{
				Names.Add(Enum->GetNameStringByIndex(Index));
			}
		}
		return FString::Join(Names, TEXT(", "));
	}

	/**
	 * Checks a JSON value against the property type before conversion.
	 * FJsonObjectConverter alone is lenient (it accepts "many" for an integer), so tool calls would silently use wrong values.
	 */
	bool ValidateJsonValue(const TSharedPtr<FJsonValue>& Value, const FProperty* Property, const FString& Path, FString& OutError)
	{
		const EJson Type = Value.IsValid() ? Value->Type : EJson::None;
		auto Reject = [&OutError, &Path, Type](const TCHAR* Expected)
		{
			OutError = FString::Printf(TEXT("'%s' must be %s, not %s."), *Path, Expected, JsonTypeName(Type));
			return false;
		};
		auto CheckNameLength = [&OutError, &Path](const FString& Text)
		{
			if (Text.Len() > MaxNameLength)
			{
				OutError = FString::Printf(TEXT("'%s' is longer than %d characters."), *Path, MaxNameLength);
				return false;
			}
			return true;
		};

		if (CastField<FObjectPropertyBase>(Property))
		{
			const bool bInstanced = Property->HasAnyPropertyFlags(CPF_PersistentInstance | CPF_InstancedReference);
			if (Type == EJson::String)
			{
				return CheckNameLength(Value->AsString());
			}
			if (Type == EJson::Null || (bInstanced && Type == EJson::Object))
			{
				return true;
			}
			return Reject(TEXT("an object path string or null"));
		}

		if (Type == EJson::Null || Type == EJson::None)
		{
			return Reject(TEXT("a value"));
		}

		if (CastField<FBoolProperty>(Property))
		{
			return Type == EJson::Boolean ? true : Reject(TEXT("a boolean"));
		}

		if (const UEnum* Enum = GetPropertyEnum(Property))
		{
			if (Type != EJson::String)
			{
				return Reject(TEXT("an enum value name"));
			}
			const FString Text = Value->AsString();
			if (Text.IsEmpty() || Text.Len() > MaxNameLength || Enum->GetValueByName(FName(*Text), EGetByNameFlags::CheckAuthoredName) == INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("'%s' must be one of %s, not '%s'."), *Path, *DescribeEnumValues(Enum), *Text.Left(64));
				return false;
			}
			return true;
		}

		const FByteProperty* ByteProperty = CastField<FByteProperty>(Property);
		if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			if (Type != EJson::Number)
			{
				return Reject(TEXT("a number"));
			}
			if (NumericProperty->IsInteger())
			{
				const double Number = Value->AsNumber();
				if (FMath::Frac(Number) != 0.0)
				{
					return Reject(TEXT("an integer"));
				}

				double Min = -9007199254740992.0;
				double Max = 9007199254740992.0;
				if (CastField<FIntProperty>(Property)) { Min = MIN_int32; Max = MAX_int32; }
				else if (CastField<FInt16Property>(Property)) { Min = MIN_int16; Max = MAX_int16; }
				else if (CastField<FInt8Property>(Property)) { Min = MIN_int8; Max = MAX_int8; }
				else if (ByteProperty) { Min = 0.0; Max = MAX_uint8; }
				else if (CastField<FUInt16Property>(Property)) { Min = 0.0; Max = MAX_uint16; }
				else if (CastField<FUInt32Property>(Property)) { Min = 0.0; Max = MAX_uint32; }
				else if (CastField<FUInt64Property>(Property)) { Min = 0.0; }

				if (Number < Min || Number > Max)
				{
					OutError = FString::Printf(TEXT("'%s' is out of range (%.0f to %.0f)."), *Path, Min, Max);
					return false;
				}
			}
			return true;
		}

		if (CastField<FNameProperty>(Property))
		{
			return Type == EJson::String ? CheckNameLength(Value->AsString()) : Reject(TEXT("a string"));
		}

		if (CastField<FStrProperty>(Property) || CastField<FTextProperty>(Property))
		{
			return Type == EJson::String ? true : Reject(TEXT("a string"));
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			const UScriptStruct* Struct = StructProperty->Struct.Get();
			if (Struct == FJsonObjectWrapper::StaticStruct())
			{
				return Type == EJson::Object ? true : Reject(TEXT("an object"));
			}

			const UScriptStruct::ICppStructOps* StructOps = Struct ? Struct->GetCppStructOps() : nullptr;
			if (StructOps && StructOps->HasExportTextItem())
			{
				return Type == EJson::String ? true : Reject(TEXT("a string in Unreal text form"));
			}

			if (Type != EJson::Object)
			{
				return Reject(TEXT("an object"));
			}

			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Value->AsObject()->Values)
			{
				const FProperty* FieldProperty = nullptr;
				for (TFieldIterator<FProperty> It(Struct); It; ++It)
				{
					if (Schema::GetJsonName(*It).Equals(Field.Key, ESearchCase::IgnoreCase) || It->GetName().Equals(Field.Key, ESearchCase::IgnoreCase))
					{
						FieldProperty = *It;
						break;
					}
				}
				if (!FieldProperty)
				{
					OutError = FString::Printf(TEXT("'%s' has no field '%s'."), *Path, *Field.Key);
					return false;
				}
				if (!ValidateJsonValue(Field.Value, FieldProperty, Path + TEXT(".") + Field.Key, OutError))
				{
					return false;
				}
			}
			return true;
		}

		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			if (Type != EJson::Array)
			{
				return Reject(TEXT("an array"));
			}
			const TArray<TSharedPtr<FJsonValue>>& Items = Value->AsArray();
			for (int32 Index = 0; Index < Items.Num(); ++Index)
			{
				if (!ValidateJsonValue(Items[Index], ArrayProperty->Inner, FString::Printf(TEXT("%s[%d]"), *Path, Index), OutError))
				{
					return false;
				}
			}
			return true;
		}

		if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			if (Type != EJson::Array)
			{
				return Reject(TEXT("an array"));
			}
			const TArray<TSharedPtr<FJsonValue>>& Items = Value->AsArray();
			for (int32 Index = 0; Index < Items.Num(); ++Index)
			{
				if (!ValidateJsonValue(Items[Index], SetProperty->ElementProp, FString::Printf(TEXT("%s[%d]"), *Path, Index), OutError))
				{
					return false;
				}
			}
			return true;
		}

		if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			if (Type != EJson::Object)
			{
				return Reject(TEXT("an object"));
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Value->AsObject()->Values)
			{
				if (Entry.Key.Len() > MaxNameLength)
				{
					OutError = FString::Printf(TEXT("'%s' has a key longer than %d characters."), *Path, MaxNameLength);
					return false;
				}
				if (!ValidateJsonValue(Entry.Value, MapProperty->ValueProp, Path + TEXT(".") + Entry.Key, OutError))
				{
					return false;
				}
			}
			return true;
		}

		return true;
	}

	bool ImportObjectReference(const TSharedPtr<FJsonValue>& JsonValue, FProperty* Property, void* Value)
	{
		FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property);
		if (!ObjectProperty || !JsonValue.IsValid())
		{
			return false;
		}

		if (JsonValue->Type == EJson::Null || (JsonValue->Type == EJson::String && JsonValue->AsString().IsEmpty()))
		{
			ObjectProperty->SetObjectPropertyValue(Value, nullptr);
			return true;
		}
		if (JsonValue->Type != EJson::String)
		{
			// Let FJsonObjectConverter handle instanced objects given as JSON objects.
			return false;
		}

		const FString Text = JsonValue->AsString();

		if (CastField<FSoftObjectProperty>(Property))
		{
			// Soft references (including soft class references) are stored without loading.
			*static_cast<FSoftObjectPtr*>(Value) = FSoftObjectPath(StripObjectPathWrapper(Text));
			return true;
		}

		const UClass* ExpectedClass = ObjectProperty->PropertyClass.Get();
		UObject* Resolved = nullptr;
		if (const FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
		{
			UClass* ResolvedClass = ResolveClass(Text);
			if (ResolvedClass && ClassProperty->MetaClass && !ResolvedClass->IsChildOf(ClassProperty->MetaClass.Get()))
			{
				RaiseToolError(TEXT("INVALID_ARGUMENT"),
					FString::Printf(TEXT("Class '%s' does not derive from %s (argument '%s')."), *ResolvedClass->GetPathName(), *ClassProperty->MetaClass->GetName(), *Schema::GetJsonName(Property)));
				ObjectProperty->SetObjectPropertyValue(Value, nullptr);
				return true;
			}
			Resolved = ResolvedClass;
		}
		else
		{
			TArray<FString> Candidates;
			if (ResolveObject(Text, ExpectedClass, Resolved, Candidates) == EResolveResult::Ambiguous)
			{
				if (Candidates.Num() > MaxListedCandidates)
				{
					Candidates.SetNum(MaxListedCandidates);
					Candidates.Add(TEXT("..."));
				}
				RaiseToolError(TEXT("AMBIGUOUS_REFERENCE"),
					FString::Printf(TEXT("'%s' matches more than one actor (argument '%s')."), *Text, *Schema::GetJsonName(Property)),
					FString::Printf(TEXT("Pass one of these object paths instead: %s"), *FString::Join(Candidates, TEXT(", "))));
				ObjectProperty->SetObjectPropertyValue(Value, nullptr);
				return true;
			}
		}

		if (!Resolved)
		{
			RaiseToolError(TEXT("NOT_FOUND"),
				FString::Printf(TEXT("'%s' does not resolve to a %s (argument '%s')."), *Text, *GetNameSafe(ExpectedClass), *Schema::GetJsonName(Property)),
				TEXT("Use an object path such as /Game/Folder/Asset.Asset, an actor label, or ActorLabel.ComponentName; actor_find lists actor paths."));
			ObjectProperty->SetObjectPropertyValue(Value, nullptr);
			return true;
		}

		if (ExpectedClass && !Resolved->IsA(ExpectedClass))
		{
			RaiseToolError(TEXT("INVALID_ARGUMENT"),
				FString::Printf(TEXT("'%s' is a %s, expected a %s (argument '%s')."), *Resolved->GetPathName(), *Resolved->GetClass()->GetName(), *ExpectedClass->GetName(), *Schema::GetJsonName(Property)));
			ObjectProperty->SetObjectPropertyValue(Value, nullptr);
			return true;
		}

		ObjectProperty->SetObjectPropertyValue(Value, Resolved);
		return true;
	}

	/** Images of the tool call being converted; the dispatcher sends them as MCP image content. */
	thread_local TArray<FAgentMcpImage>* CollectedImages = nullptr;

	/**
	 * Custom export: object references, including instanced sub-objects, become paths so results stay small and never recurse,
	 * and FAgentMcpImage values become a summary while their bytes are collected for image content.
	 */
	TSharedPtr<FJsonValue> ExportObjectReference(FProperty* Property, const void* Value)
	{
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property); StructProperty && StructProperty->Struct == FAgentMcpImage::StaticStruct())
		{
			const FAgentMcpImage& Image = *static_cast<const FAgentMcpImage*>(Value);
			const TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
			Summary->SetStringField(TEXT("mimeType"), Image.MimeType);
			Summary->SetNumberField(TEXT("width"), Image.Width);
			Summary->SetNumberField(TEXT("height"), Image.Height);
			Summary->SetNumberField(TEXT("bytes"), Image.Data.Num());
			if (CollectedImages && Image.Data.Num() > 0)
			{
				CollectedImages->Add(Image);
				Summary->SetStringField(TEXT("content"), TEXT("sent as an MCP image content item"));
			}
			return MakeShared<FJsonValueObject>(Summary);
		}

		if (CastField<FSoftObjectProperty>(Property))
		{
			const FSoftObjectPath& Path = static_cast<const FSoftObjectPtr*>(Value)->ToSoftObjectPath();
			if (Path.IsNull())
			{
				return MakeShared<FJsonValueNull>();
			}
			return MakeShared<FJsonValueString>(Path.ToString());
		}

		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			const UObject* Object = ObjectProperty->GetObjectPropertyValue(Value);
			if (!Object)
			{
				return MakeShared<FJsonValueNull>();
			}
			return MakeShared<FJsonValueString>(Object->GetPathName());
		}
		return nullptr;
	}

	/** Validates and converts one value. Failures are raised through RaiseToolError. */
	bool ImportJsonValue(const TSharedPtr<FJsonValue>& JsonValue, FProperty* Property, void* ValuePtr, const FString& DisplayName)
	{
		FString ValidationError;
		if (!ValidateJsonValue(JsonValue, Property, DisplayName, ValidationError))
		{
			RaiseToolError(TEXT("INVALID_ARGUMENT"), ValidationError, TEXT("Check the expected type in the input schema or object_list_properties."));
			return false;
		}

		const FJsonObjectConverter::CustomImportCallback ImportCallback = FJsonObjectConverter::CustomImportCallback::CreateStatic(&ImportObjectReference);
		FText FailReason;
		const bool bConverted = FJsonObjectConverter::JsonValueToUProperty(JsonValue, Property, ValuePtr, 0, 0, false, &FailReason, &ImportCallback);
		if (HasToolError())
		{
			return false;
		}
		if (!bConverted)
		{
			RaiseToolError(TEXT("INVALID_ARGUMENT"),
				FString::Printf(TEXT("'%s' could not be converted.%s%s"), *DisplayName, FailReason.IsEmpty() ? TEXT("") : TEXT(" "), *FailReason.ToString()),
				TEXT("Check the expected type in the input schema or object_list_properties."));
			return false;
		}
		return true;
	}

	bool IsAsyncResultProperty(const FProperty* Property)
	{
		const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property);
		return ObjectProperty && ObjectProperty->PropertyClass && ObjectProperty->PropertyClass->IsChildOf(UAgentMcpAsyncResult::StaticClass());
	}

	void FillError(FInvokeOutcome& Outcome, const FToolErrorScope& Scope)
	{
		Outcome.bSuccess = false;
		Outcome.ErrorCode = Scope.GetErrorCode();
		Outcome.ErrorText = Scope.GetErrorText();
		Outcome.ErrorHint = Scope.GetErrorHint();
	}
}

namespace UE::AgentMcp
{
	bool IsJsonCompatibleProperty(const FProperty* Property)
	{
		FString Reason;
		return Property && Schema::IsSupportedProperty(Property, Reason);
	}

	TSharedPtr<FJsonValue> PropertyValueToJson(const FProperty* Property, const void* ValuePtr)
	{
		if (!ValuePtr || !IsJsonCompatibleProperty(Property))
		{
			return nullptr;
		}
		const FJsonObjectConverter::CustomExportCallback ExportCallback = FJsonObjectConverter::CustomExportCallback::CreateStatic(&InvokerPrivate::ExportObjectReference);
		return FJsonObjectConverter::UPropertyToJsonValue(const_cast<FProperty*>(Property), ValuePtr, 0, 0, &ExportCallback);
	}

	TSharedRef<FJsonObject> PropertyToJsonSchema(const FProperty* Property)
	{
		return Property ? Schema::MakePropertySchema(Property, 0) : MakeShared<FJsonObject>();
	}

	namespace InvokerPrivate
	{
		bool IsEmptyJsonObjectValue(const TSharedPtr<FJsonValue>& Value)
		{
			const TSharedPtr<FJsonObject>* Nested = nullptr;
			return Value.IsValid() && Value->TryGetObject(Nested) && Nested && (!Nested->IsValid() || (*Nested)->Values.IsEmpty());
		}

		/** Walks the struct definition next to its JSON so that only result fields are compacted, never wrapped data values. */
		void CompactStructJson(const UStruct* Struct, FJsonObject& Object)
		{
			if (!Struct)
			{
				return;
			}

			for (TFieldIterator<FProperty> It(Struct); It; ++It)
			{
				const FProperty* Property = *It;
				const FString Key = Schema::GetJsonName(Property);
				const TSharedPtr<FJsonValue>* Found = Object.Values.Find(Key);
				if (!Found || !Found->IsValid())
				{
					continue;
				}
				const TSharedPtr<FJsonValue> Value = *Found;

				if (CastField<FStrProperty>(Property) || CastField<FNameProperty>(Property) || CastField<FTextProperty>(Property))
				{
					FString Text;
					if (Value->TryGetString(Text) && Text.IsEmpty())
					{
						Object.Values.Remove(Key);
					}
				}
				else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
				{
					if (StructProperty->Struct != FJsonObjectWrapper::StaticStruct())
					{
						const TSharedPtr<FJsonObject>* Nested = nullptr;
						if (Value->TryGetObject(Nested) && Nested && Nested->IsValid())
						{
							CompactStructJson(StructProperty->Struct.Get(), **Nested);
						}
					}
					if (IsEmptyJsonObjectValue(Value))
					{
						Object.Values.Remove(Key);
					}
				}
				else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
				{
					const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
					if (!Value->TryGetArray(Items) || !Items)
					{
						continue;
					}
					if (Items->IsEmpty())
					{
						Object.Values.Remove(Key);
						continue;
					}
					const FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProperty->Inner);
					if (InnerStruct && InnerStruct->Struct != FJsonObjectWrapper::StaticStruct())
					{
						for (const TSharedPtr<FJsonValue>& Item : *Items)
						{
							const TSharedPtr<FJsonObject>* ItemObject = nullptr;
							if (Item.IsValid() && Item->TryGetObject(ItemObject) && ItemObject && ItemObject->IsValid())
							{
								CompactStructJson(InnerStruct->Struct.Get(), **ItemObject);
							}
						}
					}
				}
				else if (CastField<FMapProperty>(Property) || CastField<FSetProperty>(Property))
				{
					const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
					if (IsEmptyJsonObjectValue(Value) || (Value->TryGetArray(Items) && Items && Items->IsEmpty()))
					{
						Object.Values.Remove(Key);
					}
				}
			}
		}
	}

	void CompactToolResult(const UFunction* Function, FJsonObject& Result)
	{
		if (const FStructProperty* ReturnStruct = CastField<FStructProperty>(Function ? Function->GetReturnProperty() : nullptr))
		{
			InvokerPrivate::CompactStructJson(ReturnStruct->Struct.Get(), Result);
		}
	}

	TSharedRef<FJsonObject> StructToJson(const UStruct* Struct, const void* Data)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		if (Struct && Data)
		{
			const FJsonObjectConverter::CustomExportCallback ExportCallback = FJsonObjectConverter::CustomExportCallback::CreateStatic(&InvokerPrivate::ExportObjectReference);
			FJsonObjectConverter::UStructToJsonObject(Struct, Data, Object, 0, 0, &ExportCallback);
			InvokerPrivate::CompactStructJson(Struct, *Object);
		}
		return Object;
	}

	bool JsonToPropertyValue(const TSharedPtr<FJsonValue>& JsonValue, FProperty* Property, void* ValuePtr, const FString& DisplayName, FString& OutErrorCode, FString& OutError)
	{
		// A nested scope keeps the error local, so callers can report several invalid values at once.
		FToolErrorScope Scope;
		if (Property && ValuePtr && InvokerPrivate::ImportJsonValue(JsonValue, Property, ValuePtr, DisplayName) && !Scope.HasError())
		{
			return true;
		}
		OutErrorCode = Scope.HasError() ? Scope.GetErrorCode() : FString(TEXT("INVALID_ARGUMENT"));
		OutError = Scope.HasError() ? Scope.GetErrorText() : FString::Printf(TEXT("'%s' could not be converted."), *DisplayName);
		return false;
	}

	bool IsAsyncToolFunction(const UFunction* Function)
	{
		return Function && InvokerPrivate::IsAsyncResultProperty(Function->GetReturnProperty());
	}

	FInvokeOutcome InvokeToolFunction(UClass* ToolsetClass, UFunction* Function, const TSharedRef<FJsonObject>& Arguments)
	{
		using namespace InvokerPrivate;

		check(IsInGameThread());

		FInvokeOutcome Outcome;
		UObject* DefaultObject = ToolsetClass ? ToolsetClass->GetDefaultObject() : nullptr;
		if (!DefaultObject || !Function)
		{
			Outcome.ErrorCode = TEXT("TOOL_UNAVAILABLE");
			Outcome.ErrorText = TEXT("The tool function is no longer available (the module may have been reloaded).");
			return Outcome;
		}

		// Reject unknown argument names so typos surface instead of being silently ignored.
		TArray<FString> InputNames;
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			if (Schema::IsInputParameter(*It))
			{
				InputNames.Add(Schema::GetJsonName(*It));
			}
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Argument : Arguments->Values)
		{
			if (!InputNames.Contains(Argument.Key))
			{
				Outcome.ErrorCode = TEXT("INVALID_ARGUMENT");
				Outcome.ErrorText = FString::Printf(TEXT("Unknown argument '%s'."), *Argument.Key);
				Outcome.ErrorHint = InputNames.Num() > 0
					? FString::Printf(TEXT("Expected arguments: %s."), *FString::Join(InputNames, TEXT(", ")))
					: TEXT("This tool takes no arguments.");
				return Outcome;
			}
		}

		const int32 ParmsSize = Function->ParmsSize;
		uint8* Params = nullptr;
		if (ParmsSize > 0)
		{
			Params = static_cast<uint8*>(FMemory_Alloca_Aligned(ParmsSize, Function->GetMinAlignment()));
			FMemory::Memzero(Params, ParmsSize);
			for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				It->InitializeValue_InContainer(Params);
			}
		}

		ON_SCOPE_EXIT
		{
			if (Params)
			{
				for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
				{
					It->DestroyValue_InContainer(Params);
				}
			}
		};

		FToolErrorScope ErrorScope;

		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			FProperty* Parameter = *It;
			if (!Schema::IsInputParameter(Parameter))
			{
				continue;
			}

			const FString JsonName = Schema::GetJsonName(Parameter);
			void* ValuePtr = Parameter->ContainerPtrToValuePtr<void>(Params);

			if (const TSharedPtr<FJsonValue>* ArgumentValue = Arguments->Values.Find(JsonName); ArgumentValue && ArgumentValue->IsValid())
			{
				if (!ImportJsonValue(*ArgumentValue, Parameter, ValuePtr, JsonName))
				{
					break;
				}
				continue;
			}

			FString DefaultText;
			if (Schema::TryGetParameterDefault(Function, Parameter, DefaultText))
			{
				if (!DefaultText.IsEmpty() && !Parameter->ImportText_Direct(*DefaultText, ValuePtr, nullptr, PPF_None))
				{
					RaiseToolError(TEXT("TOOL_DEFINITION_ERROR"), FString::Printf(TEXT("The default value '%s' of '%s' could not be imported."), *DefaultText, *JsonName));
					break;
				}
				continue;
			}

			if (Schema::IsAutoCreateRefTerm(Function, Parameter))
			{
				// Optional reference parameter (AutoCreateRefTerm): the tool receives a default-constructed value.
				continue;
			}

			RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("Missing required argument '%s'."), *JsonName));
			break;
		}

		if (ErrorScope.HasError())
		{
			FillError(Outcome, ErrorScope);
			return Outcome;
		}

		DefaultObject->ProcessEvent(Function, Params);

		if (ErrorScope.HasError())
		{
			FillError(Outcome, ErrorScope);
			return Outcome;
		}

		const FProperty* ReturnProperty = Function->GetReturnProperty();
		if (IsAsyncResultProperty(ReturnProperty))
		{
			UAgentMcpAsyncResult* Pending = Cast<UAgentMcpAsyncResult>(CastField<FObjectProperty>(ReturnProperty)->GetObjectPropertyValue_InContainer(Params));
			if (!Pending)
			{
				Outcome.ErrorCode = TEXT("TOOL_ERROR");
				Outcome.ErrorText = FString::Printf(TEXT("%s returned no result."), *Function->GetName());
				return Outcome;
			}
			Outcome.bSuccess = true;
			Outcome.AsyncResult = TStrongObjectPtr<UAgentMcpAsyncResult>(Pending);
			return Outcome;
		}

		const FJsonObjectConverter::CustomExportCallback ExportCallback = FJsonObjectConverter::CustomExportCallback::CreateStatic(&ExportObjectReference);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		const TGuardValue<TArray<FAgentMcpImage>*> ImageCollectionGuard(CollectedImages, &Outcome.Images);

		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			FProperty* Parameter = *It;
			if (!Schema::IsOutputParameter(Parameter))
			{
				continue;
			}

			const void* ValuePtr = Parameter->ContainerPtrToValuePtr<void>(Params);
			if (Parameter->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				if (const FStructProperty* StructProperty = CastField<FStructProperty>(Parameter))
				{
					FJsonObjectConverter::UStructToJsonObject(StructProperty->Struct.Get(), ValuePtr, Result, 0, 0, &ExportCallback);
				}
				else
				{
					Result->SetField(TEXT("returnValue"), FJsonObjectConverter::UPropertyToJsonValue(Parameter, ValuePtr, 0, 0, &ExportCallback));
				}
			}
			else
			{
				Result->SetField(Schema::GetJsonName(Parameter), FJsonObjectConverter::UPropertyToJsonValue(Parameter, ValuePtr, 0, 0, &ExportCallback));
			}
		}

		Outcome.bSuccess = true;
		Outcome.Result = Result;
		return Outcome;
	}
}
