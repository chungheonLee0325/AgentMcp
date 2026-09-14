#include "AgentMcpUmgTools.h"

#include "AgentMcpJson.h"
#include "AgentMcpToolsCommon.h"

#include "Animation/WidgetAnimation.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/NamedSlotInterface.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "IAssetTools.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/PropertyAccessUtil.h"
#include "UObject/UObjectGlobals.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditorUtils.h"
#include "WidgetBlueprintFactory.h"

namespace UE::AgentMcp::UmgToolsPrivate
{
	constexpr int32 MaxTreeDepth = 64;
	constexpr int32 MaxListedWidgets = 2000;
	constexpr int32 MaxPropertiesPerObject = 40;

	/** Widget entries that one umg_add_widgets call accepts, children included. */
	constexpr int32 MaxAddedWidgets = 500;

	/** Bound for generated name suffixes and walks up the tree, as a guard against corrupt data. */
	constexpr int32 MaxSearchSteps = 100000;

	/** Properties a designer can edit on the widget template, excluding transient and deprecated ones. */
	bool ShouldDescribeProperty(const FProperty* Property)
	{
		return Property->HasAnyPropertyFlags(CPF_Edit)
			&& !Property->HasAnyPropertyFlags(CPF_Transient | CPF_DisableEditOnTemplate | CPF_Deprecated)
			&& IsJsonCompatibleProperty(Property);
	}

	/** Property values that differ from the class defaults. */
	TSharedRef<FJsonObject> DescribeNonDefaultProperties(const UObject* Object)
	{
		const TSharedRef<FJsonObject> Values = MakeShared<FJsonObject>();
		const UObject* Defaults = Object->GetClass()->GetDefaultObject();
		int32 Count = 0;
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			const FProperty* Property = *It;
			if (!ShouldDescribeProperty(Property) || (Defaults && Property->Identical_InContainer(Object, Defaults)))
			{
				continue;
			}
			if (Count++ >= MaxPropertiesPerObject)
			{
				Values->SetBoolField(TEXT("_truncated"), true);
				break;
			}
			const TSharedPtr<FJsonValue> Value = PropertyValueToJson(Property, Property->ContainerPtrToValuePtr<void>(Object));
			if (Value.IsValid())
			{
				Values->SetField(Property->GetName(), Value);
			}
		}
		return Values;
	}

	/** Widgets of parent Widget Blueprints also satisfy BindWidget properties. */
	const UWidget* FindWidgetInHierarchy(const UWidgetBlueprint* WidgetBlueprint, const FName WidgetName)
	{
		if (WidgetBlueprint->WidgetTree)
		{
			if (const UWidget* Widget = WidgetBlueprint->WidgetTree->FindWidget(WidgetName))
			{
				return Widget;
			}
		}
		for (const UClass* Class = WidgetBlueprint->ParentClass; Class && !Class->HasAnyClassFlags(CLASS_Native); Class = Class->GetSuperClass())
		{
			if (const UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Class))
			{
				if (const UWidgetTree* Tree = GeneratedClass->GetWidgetTreeArchetype())
				{
					if (const UWidget* Widget = Tree->FindWidget(WidgetName))
					{
						return Widget;
					}
				}
			}
		}
		return nullptr;
	}

	struct FWalkContext
	{
		FAgentMcpWidgetBlueprintDetails& Details;
		int32 MaxDepth = 32;
		int32 MaxWidgets = 300;
		bool bIncludeSlots = true;
		bool bIncludeProperties = false;
		TSet<const UWidget*> Visited;
	};

	/**
	 * Lists a widget and its descendants in pre-order: first the children of a panel in child order, then the content of each
	 * named slot. An explicit stack keeps deep trees from exhausting the call stack.
	 */
	void Walk(FWalkContext& Context, const UWidget* Root, int32 RootDepth, const FString& RootParentName, const FName RootSlotName)
	{
		struct FPendingWidget
		{
			const UWidget* Widget = nullptr;
			int32 Depth = 0;
			FString ParentName;
			FName SlotName;
		};

		TArray<FPendingWidget> Pending;
		Pending.Add({ Root, RootDepth, RootParentName, RootSlotName });
		TArray<FPendingWidget> Children;

		while (Pending.Num() > 0)
		{
			const FPendingWidget Current = Pending.Pop();
			const UWidget* Widget = Current.Widget;
			if (!Widget || Context.Visited.Contains(Widget))
			{
				continue;
			}
			Context.Visited.Add(Widget);
			if (Context.Details.Widgets.Num() >= Context.MaxWidgets)
			{
				Context.Details.bTruncated = true;
				return;
			}

			FAgentMcpWidgetNode& Node = Context.Details.Widgets.AddDefaulted_GetRef();
			Node.Index = Context.Details.Widgets.Num() - 1;
			Node.Depth = Current.Depth;
			Node.Name = Widget->GetName();
			Node.ClassName = Widget->GetClass()->GetName();
			if (Widget->IsA<UUserWidget>())
			{
				Node.WidgetClass = Widget->GetClass()->GetPathName();
			}
			Node.Parent = Current.ParentName;
			if (!Current.SlotName.IsNone())
			{
				Node.NamedSlot = Current.SlotName.ToString();
			}
			Node.bIsVariable = Widget->bIsVariable;
			if (Context.bIncludeSlots && Widget->Slot)
			{
				Node.Slot.ClassName = Widget->Slot->GetClass()->GetName();
				Node.Slot.Properties.JsonObject = DescribeNonDefaultProperties(Widget->Slot);
			}
			if (Context.bIncludeProperties)
			{
				Node.Properties.JsonObject = DescribeNonDefaultProperties(Widget);
			}

			// Children in output order: panel children, then named slot content.
			Children.Reset();
			const FString WidgetName = Widget->GetName();
			if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
			{
				for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
				{
					Children.Add({ Panel->GetChildAt(ChildIndex), Current.Depth + 1, WidgetName, NAME_None });
				}
			}
			if (const INamedSlotInterface* NamedSlotHost = Cast<const INamedSlotInterface>(Widget))
			{
				TArray<FName> SlotNames;
				NamedSlotHost->GetSlotNames(SlotNames);
				for (const FName ContentSlotName : SlotNames)
				{
					if (const UWidget* Content = NamedSlotHost->GetContentForSlot(ContentSlotName))
					{
						Children.Add({ Content, Current.Depth + 1, WidgetName, ContentSlotName });
					}
				}
			}

			if (Children.Num() > 0 && Current.Depth >= Context.MaxDepth)
			{
				Context.Details.bTruncated = true;
				continue;
			}
			// The stack is last in, first out: push in reverse so that the first child is listed next.
			for (int32 ChildIndex = Children.Num() - 1; ChildIndex >= 0; --ChildIndex)
			{
				Pending.Add(MoveTemp(Children[ChildIndex]));
			}
		}
	}

	/** Fills the BindWidget properties of the parent classes (when OutBindings is given) and the required ones the tree does not satisfy. */
	void CollectBindWidgets(const UWidgetBlueprint* WidgetBlueprint, TArray<FAgentMcpBindWidget>* OutBindings, TArray<FString>& OutMissing)
	{
		const UClass* ParentClass = WidgetBlueprint->ParentClass;
		if (!ParentClass)
		{
			return;
		}
		for (TFieldIterator<FObjectPropertyBase> It(ParentClass); It; ++It)
		{
			const FObjectPropertyBase* Property = *It;
			bool bOptional = false;
			if (!FWidgetBlueprintEditorUtils::IsBindWidgetProperty(Property, bOptional))
			{
				continue;
			}

			FAgentMcpBindWidget Binding;
			Binding.Property = Property->GetName();
			Binding.DeclaredIn = GetNameSafe(Property->GetOwnerClass());
			Binding.ExpectedClass = GetNameSafe(Property->PropertyClass);
			Binding.bOptional = bOptional;
			if (const UWidget* BoundWidget = FindWidgetInHierarchy(WidgetBlueprint, Property->GetFName()))
			{
				Binding.bBound = true;
				Binding.WidgetClassName = BoundWidget->GetClass()->GetName();
				Binding.bTypeMatches = Property->PropertyClass && BoundWidget->IsA(Property->PropertyClass);
			}
			if (!bOptional && !Binding.bTypeMatches)
			{
				OutMissing.Add(Binding.Property);
			}
			if (OutBindings)
			{
				OutBindings->Add(MoveTemp(Binding));
			}
		}
	}

	/** Problems found while checking a request, reported together as one error. */
	struct FProblemList
	{
		TArray<FString> Messages;
		TSet<FString> Codes;

		void Add(const FString& Message, const TCHAR* Code)
		{
			Messages.Add(Message);
			Codes.Add(Code);
		}

		bool IsEmpty() const
		{
			return Messages.IsEmpty();
		}
	};

	/** The widget tree of a project Widget Blueprint; raises a tool error and returns null when tools cannot change it. */
	UWidgetTree* RequireEditableWidgetTree(UWidgetBlueprint* WidgetBlueprint, FString& OutWidgetBlueprintPath)
	{
		if (!Tools::RequireObject(WidgetBlueprint, TEXT("widgetBlueprint")) || !Tools::RequireProjectContent(WidgetBlueprint))
		{
			return nullptr;
		}
		OutWidgetBlueprintPath = WidgetBlueprint->GetPathName();
		if (!WidgetBlueprint->WidgetTree)
		{
			RaiseToolError(TEXT("NOT_SUPPORTED"), FString::Printf(TEXT("%s has no widget tree."), *OutWidgetBlueprintPath));
			return nullptr;
		}
		return WidgetBlueprint->WidgetTree;
	}

	/** A widget of the Widget Blueprint's own tree by name, or null. */
	UWidget* FindOwnWidget(const UWidgetBlueprint* WidgetBlueprint, const FString& Name)
	{
		const FString Trimmed = Name.TrimStartAndEnd();
		if (!WidgetBlueprint->WidgetTree || Trimmed.IsEmpty() || Trimmed.Len() >= NAME_SIZE)
		{
			return nullptr;
		}
		const FName LookupName(*Trimmed, FNAME_Find);
		return LookupName.IsNone() ? nullptr : WidgetBlueprint->WidgetTree->FindWidget(LookupName);
	}

	/** Problem text for a name that is not a widget of the Widget Blueprint's own tree. */
	FString DescribeMissingWidget(const UWidgetBlueprint* WidgetBlueprint, const FString& Name)
	{
		const FString Trimmed = Name.TrimStartAndEnd().Left(128);
		const FName LookupName(*Trimmed, FNAME_Find);
		if (!LookupName.IsNone() && FindWidgetInHierarchy(WidgetBlueprint, LookupName))
		{
			return FString::Printf(TEXT("'%s' belongs to a parent Widget Blueprint and can only be changed there"), *Trimmed);
		}
		return FString::Printf(TEXT("%s has no widget named '%s'"), *WidgetBlueprint->GetName(), *Trimmed);
	}

	/** The container of a widget: its parent panel, or the widget whose named slot holds it. Null for a top-level widget. */
	UWidget* GetContainer(UWidget* Widget, UWidgetTree* WidgetTree)
	{
		if (UPanelWidget* Parent = Widget->GetParent())
		{
			return Parent;
		}
		return FWidgetBlueprintEditorUtils::FindNamedSlotHostWidgetForContent(Widget, WidgetTree);
	}

	/** A class from a class name or path, a Widget Blueprint object path, or a Widget Blueprint package path. */
	UClass* ResolveWidgetClass(const FString& Text)
	{
		const FString Trimmed = Text.TrimStartAndEnd();
		UClass* Class = ResolveClassName(Trimmed);
		if (!Class && Trimmed.StartsWith(TEXT("/")) && !Trimmed.Contains(TEXT(".")))
		{
			// "/Game/UI/WBP_Item" names the package; the Widget Blueprint inside it has the same name.
			Class = ResolveClassName(Trimmed + TEXT(".") + FPackageName::GetShortName(Trimmed));
		}
		return Class;
	}

	/** Empty when widgets of the class can be added to the Widget Blueprint; otherwise the reason. */
	FString GetWidgetClassProblem(const UClass* Class, const UWidgetBlueprint* WidgetBlueprint)
	{
		if (!Class->IsChildOf(UWidget::StaticClass()))
		{
			return TEXT("is not a widget class");
		}
		if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			return TEXT("is abstract or deprecated");
		}
		if (Class->HasAnyFlags(RF_Transient) && Class->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
		{
			return TEXT("is the skeleton class of a Blueprint; use the Widget Blueprint instead");
		}
		const UClass* OwnClass = WidgetBlueprint->GeneratedClass;
		if (UBlueprint::GetBlueprintFromClass(Class) == WidgetBlueprint || (OwnClass && Class->IsChildOf(OwnClass)))
		{
			return TEXT("is this Widget Blueprint or derives from it, and a widget cannot contain itself");
		}
		return FString();
	}

	/** The skeleton class still has a widget variable of this name, left from a widget that was removed or renamed since the last compile. */
	bool IsStaleWidgetVariable(const UWidgetBlueprint* WidgetBlueprint, const FName Name)
	{
		const UClass* SkeletonClass = WidgetBlueprint->SkeletonGeneratedClass;
		const FObjectPropertyBase* Property = SkeletonClass ? FindFProperty<FObjectPropertyBase>(SkeletonClass, Name) : nullptr;
		return Property && Property->GetOwnerClass() == SkeletonClass && Property->PropertyClass && Property->PropertyClass->IsChildOf(UWidget::StaticClass())
			&& FBlueprintEditorUtils::FindNewVariableIndex(WidgetBlueprint, Name) == INDEX_NONE;
	}

	FString DescribeNameValidatorResult(const FString& Name, EValidatorResult Result)
	{
		switch (Result)
		{
		case EValidatorResult::EmptyName:
			return TEXT("the name is empty");
		case EValidatorResult::TooLong:
			return FString::Printf(TEXT("'%s' is longer than Blueprint names may be"), *Name);
		case EValidatorResult::ContainsInvalidCharacters:
			return FString::Printf(TEXT("'%s' contains characters that Blueprint names cannot use"), *Name);
		default:
			return FString::Printf(TEXT("'%s' is already used by a variable, function or graph of the Widget Blueprint"), *Name);
		}
	}

	/** Empty when a new widget of the class may take the name; otherwise the reason. */
	FString GetNewWidgetNameProblem(const UWidgetBlueprint* WidgetBlueprint, FKismetNameValidator& Validator, const FString& Name, const UClass* WidgetClass)
	{
		if (Name.Len() >= NAME_SIZE || !FName::IsValidXName(Name, INVALID_OBJECTNAME_CHARACTERS))
		{
			return FString::Printf(TEXT("'%s' is not a valid object name"), *Name.Left(128));
		}
		const FName WidgetName(*Name);
		if (FindWidgetInHierarchy(WidgetBlueprint, WidgetName))
		{
			return FString::Printf(TEXT("a widget named '%s' already exists"), *Name);
		}

		// A BindWidget property of the parent class expects a widget of its class under this name.
		const UClass* ParentClass = WidgetBlueprint->ParentClass;
		const FObjectPropertyBase* Property = ParentClass ? CastField<FObjectPropertyBase>(ParentClass->FindPropertyByName(WidgetName)) : nullptr;
		if (Property && FWidgetBlueprintEditorUtils::IsBindWidgetProperty(Property))
		{
			if (Property->PropertyClass && !WidgetClass->IsChildOf(Property->PropertyClass))
			{
				return FString::Printf(TEXT("the BindWidget property %s.%s expects a %s"), *GetNameSafe(Property->GetOwnerClass()), *Name, *GetNameSafe(Property->PropertyClass));
			}
			return FString();
		}

		const EValidatorResult Result = Validator.IsValid(Name);
		if (Result == EValidatorResult::Ok || (Result == EValidatorResult::AlreadyInUse && IsStaleWidgetVariable(WidgetBlueprint, WidgetName)))
		{
			return FString();
		}
		return DescribeNameValidatorResult(Name, Result);
	}

	/** A free name of the form ClassName_N, or NAME_None. */
	FName MakeWidgetName(const UWidgetBlueprint* WidgetBlueprint, FKismetNameValidator& Validator, const TSet<FName>& ReservedNames, const UClass* WidgetClass)
	{
		FString BaseName = WidgetClass->GetName();
		BaseName.RemoveFromEnd(TEXT("_C"));
		for (int32 Suffix = 0; Suffix < MaxSearchSteps; ++Suffix)
		{
			const FString Candidate = FString::Printf(TEXT("%s_%d"), *BaseName, Suffix);
			const FName CandidateName(*Candidate);
			if (!ReservedNames.Contains(CandidateName) && !FindWidgetInHierarchy(WidgetBlueprint, CandidateName) && Validator.IsValid(Candidate) == EValidatorResult::Ok)
			{
				return CandidateName;
			}
		}
		return NAME_None;
	}

	/**
	 * Moves an object out of the widget tree that still uses the name without being part of the tree, such as a widget whose addition was
	 * undone or rolled back. Creating a widget under the same name would otherwise replace that object. The undo buffer no longer refers
	 * to it: beginning the transaction of this call discarded the redo history.
	 */
	void ReleaseStaleWidgetName(UWidgetTree* WidgetTree, const FName Name)
	{
		if (UObject* Stale = StaticFindObjectFast(UObject::StaticClass(), WidgetTree, Name))
		{
			Stale->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
		}
	}

	const TCHAR* JsonTypeName(EJson Type)
	{
		switch (Type)
		{
		case EJson::String:
			return TEXT("a string");
		case EJson::Number:
			return TEXT("a number");
		case EJson::Boolean:
			return TEXT("a boolean");
		case EJson::Array:
			return TEXT("an array");
		case EJson::Object:
			return TEXT("an object");
		default:
			return TEXT("null");
		}
	}

	/** The field when it has the expected JSON type. A missing or null field gives null; a field of another type also adds a problem. */
	const TSharedPtr<FJsonValue>* FindTypedField(const FJsonObject& Object, const TCHAR* FieldName, EJson Type, const FString& Label, FProblemList& Problems)
	{
		const TSharedPtr<FJsonValue>* Value = Object.Values.Find(FieldName);
		if (!Value || !Value->IsValid() || (*Value)->IsNull())
		{
			return nullptr;
		}
		if ((*Value)->Type != Type)
		{
			Problems.Add(FString::Printf(TEXT("%s: '%s' must be %s, not %s"), *Label, FieldName, JsonTypeName(Type), JsonTypeName((*Value)->Type)), TEXT("INVALID_ARGUMENT"));
			return nullptr;
		}
		return Value;
	}

	void ReportUnknownFields(const FJsonObject& Object, std::initializer_list<const TCHAR*> KnownFields, const FString& Label, FProblemList& Problems)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object.Values)
		{
			bool bKnown = false;
			FString Expected;
			for (const TCHAR* Field : KnownFields)
			{
				bKnown |= Pair.Key.Equals(Field, ESearchCase::CaseSensitive);
				Expected += Expected.IsEmpty() ? FString(Field) : FString::Printf(TEXT(", %s"), Field);
			}
			if (!bKnown)
			{
				Problems.Add(FString::Printf(TEXT("%s has the unknown field '%s' (expected %s)"), *Label, *Pair.Key.Left(64), *Expected), TEXT("INVALID_ARGUMENT"));
			}
		}
	}

	/** Checks values for an object without changing it; problems are prefixed with Label. */
	void CheckValues(const UObject* Object, const FJsonObject& Values, const FString& Label, bool bCheckAsInstance, FProblemList& Problems)
	{
		Tools::FPreparedPropertyValues Prepared;
		TArray<FString> Messages;
		Prepared.Prepare(Object, Values, Messages, Problems.Codes, bCheckAsInstance);
		for (const FString& Message : Messages)
		{
			Problems.Messages.Add(FString::Printf(TEXT("%s: %s"), *Label, *Message));
		}
	}

	/** Converts and applies values to a widget or slot with Modify and edit notifications. Raises a tool error on failure. */
	bool ApplyValues(UObject* Object, const FJsonObject& Values, const FString& Label, bool& bOutChanged)
	{
		Tools::FPreparedPropertyValues Prepared;
		TArray<FString> Problems;
		TSet<FString> ProblemCodes;
		if (!Prepared.Prepare(Object, Values, Problems, ProblemCodes))
		{
			Tools::RaiseProblems(FString::Printf(TEXT("%s could not be changed"), *Label), Problems, ProblemCodes);
			return false;
		}

		for (const Tools::FPreparedPropertyValues::FEntry& Entry : Prepared.GetEntries())
		{
			if (PropertyAccessUtil::IsCompletePropertyIdentical(Entry.Property, Entry.Value, Entry.Property, Entry.Property->ContainerPtrToValuePtr<void>(Object)))
			{
				continue;
			}
			// Record the object for undo; PropertyAccessUtil then emits PreEditChange and PostEditChangeChainProperty around the copy.
			Object->SetFlags(RF_Transactional);
			Object->Modify();
			const EPropertyAccessResultFlags SetResult = PropertyAccessUtil::SetPropertyValue_Object(
				Entry.Property, Object, Entry.Property, Entry.Value, INDEX_NONE, PropertyAccessUtil::EditorReadOnlyFlags, EPropertyAccessChangeNotifyMode::Default);
			if (SetResult != EPropertyAccessResultFlags::Success)
			{
				RaiseToolError(TEXT("PROPERTY_WRITE_FAILED"), FString::Printf(TEXT("Setting '%s' on %s failed."), *Entry.Property->GetName(), *Label));
				return false;
			}
			bOutChanged = true;
		}
		return true;
	}

	/**
	 * The part of a value that a request names: the fields of an object that the requested object lists, recursively. Field names match
	 * case-insensitively because results use camelCase. Arrays and other values are kept whole.
	 */
	TSharedPtr<FJsonValue> ProjectToRequest(const TSharedPtr<FJsonValue>& Value, const TSharedPtr<FJsonValue>& Requested)
	{
		if (!Value.IsValid() || !Requested.IsValid() || Value->Type != EJson::Object || Requested->Type != EJson::Object)
		{
			return Value;
		}
		const TSharedRef<FJsonObject> Projected = MakeShared<FJsonObject>();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& RequestedField : Requested->AsObject()->Values)
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Value->AsObject()->Values)
			{
				if (Field.Key.Equals(RequestedField.Key, ESearchCase::IgnoreCase))
				{
					Projected->SetField(Field.Key, ProjectToRequest(Field.Value, RequestedField.Value));
					break;
				}
			}
		}
		return MakeShared<FJsonValueObject>(Projected);
	}

	/** Current values of the properties named in Values, limited to the requested struct fields so that results stay small. */
	TSharedRef<FJsonObject> ReadRequestedValues(const UObject* Object, const FJsonObject& Values)
	{
		const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Values.Values)
		{
			if (const FProperty* Property = Tools::FindPropertyByName(Object, Pair.Key))
			{
				Result->SetField(Property->GetName(), ProjectToRequest(Tools::ReadPropertyValue(Object, Property), Pair.Value));
			}
		}
		return Result;
	}

	FAgentMcpWidgetNode MakeEditedWidgetNode(UWidget* Widget, UWidgetTree* WidgetTree, int32 Index, int32 Depth)
	{
		FAgentMcpWidgetNode Node;
		Node.Index = Index;
		Node.Depth = Depth;
		Node.Name = Widget->GetName();
		Node.ClassName = Widget->GetClass()->GetName();
		if (Widget->IsA<UUserWidget>())
		{
			Node.WidgetClass = Widget->GetClass()->GetPathName();
		}
		if (const UWidget* Container = GetContainer(Widget, WidgetTree))
		{
			Node.Parent = Container->GetName();
		}
		Node.bIsVariable = Widget->bIsVariable;
		if (Widget->Slot)
		{
			Node.Slot.ClassName = Widget->Slot->GetClass()->GetName();
		}
		return Node;
	}

	/** A checked umg_add_widgets entry. */
	struct FWidgetPlan
	{
		UClass* Class = nullptr;
		FName Name;
		bool bIsVariable = false;
		TSharedPtr<FJsonObject> Properties;
		TSharedPtr<FJsonObject> Slot;
		TArray<FWidgetPlan> Children;
	};

	struct FAddContext
	{
		explicit FAddContext(const UWidgetBlueprint* InWidgetBlueprint)
			: WidgetBlueprint(InWidgetBlueprint)
			, Validator(InWidgetBlueprint)
		{
		}

		const UWidgetBlueprint* WidgetBlueprint;
		FKismetNameValidator Validator;
		TSet<FName> ReservedNames;
		FProblemList Problems;
		int32 EntryCount = 0;
	};

	/** Checks one entry and its children for a parent panel of class ParentClass (null for the root widget) and fills OutPlan. */
	void PlanWidget(FAddContext& Context, const FJsonObject& Entry, const UClass* ParentClass, const FString& Where, FWidgetPlan& OutPlan)
	{
		FProblemList& Problems = Context.Problems;
		if (++Context.EntryCount > MaxAddedWidgets)
		{
			if (Context.EntryCount == MaxAddedWidgets + 1)
			{
				Problems.Add(FString::Printf(TEXT("one call adds at most %d widgets; split the request"), MaxAddedWidgets), TEXT("INVALID_ARGUMENT"));
			}
			return;
		}

		FString NameText;
		if (const TSharedPtr<FJsonValue>* NameValue = FindTypedField(Entry, TEXT("name"), EJson::String, Where, Problems))
		{
			NameText = (*NameValue)->AsString().TrimStartAndEnd();
		}
		const FString Label = NameText.IsEmpty() ? Where : FString::Printf(TEXT("%s '%s'"), *Where, *NameText.Left(128));
		ReportUnknownFields(Entry, { TEXT("class"), TEXT("name"), TEXT("isVariable"), TEXT("properties"), TEXT("slot"), TEXT("children") }, Label, Problems);

		const TSharedPtr<FJsonValue>* ClassValue = FindTypedField(Entry, TEXT("class"), EJson::String, Label, Problems);
		if (!ClassValue)
		{
			const TSharedPtr<FJsonValue>* RawClass = Entry.Values.Find(TEXT("class"));
			if (!RawClass || !RawClass->IsValid() || (*RawClass)->IsNull())
			{
				Problems.Add(FString::Printf(TEXT("%s needs a 'class', such as TextBlock"), *Label), TEXT("INVALID_ARGUMENT"));
			}
			return;
		}
		const FString ClassText = (*ClassValue)->AsString();
		OutPlan.Class = ResolveWidgetClass(ClassText);
		if (!OutPlan.Class)
		{
			Problems.Add(FString::Printf(TEXT("%s: no class '%s' was found"), *Label, *ClassText.Left(256)), TEXT("NOT_FOUND"));
			return;
		}
		const FString ClassProblem = GetWidgetClassProblem(OutPlan.Class, Context.WidgetBlueprint);
		if (!ClassProblem.IsEmpty())
		{
			Problems.Add(FString::Printf(TEXT("%s: %s %s"), *Label, *OutPlan.Class->GetName(), *ClassProblem), TEXT("INVALID_ARGUMENT"));
			return;
		}

		if (NameText.IsEmpty())
		{
			OutPlan.Name = MakeWidgetName(Context.WidgetBlueprint, Context.Validator, Context.ReservedNames, OutPlan.Class);
			if (OutPlan.Name.IsNone())
			{
				Problems.Add(FString::Printf(TEXT("%s: no free name was found for a %s; give the entry a name"), *Label, *OutPlan.Class->GetName()), TEXT("INVALID_ARGUMENT"));
			}
		}
		else
		{
			FString NameProblem = GetNewWidgetNameProblem(Context.WidgetBlueprint, Context.Validator, NameText, OutPlan.Class);
			if (NameProblem.IsEmpty() && Context.ReservedNames.Contains(FName(*NameText)))
			{
				NameProblem = TEXT("another entry of this call uses the same name");
			}
			if (NameProblem.IsEmpty())
			{
				OutPlan.Name = FName(*NameText);
			}
			else
			{
				Problems.Add(FString::Printf(TEXT("%s: %s"), *Label, *NameProblem), TEXT("INVALID_ARGUMENT"));
			}
		}
		if (!OutPlan.Name.IsNone())
		{
			Context.ReservedNames.Add(OutPlan.Name);
		}

		if (const TSharedPtr<FJsonValue>* VariableValue = FindTypedField(Entry, TEXT("isVariable"), EJson::Boolean, Label, Problems))
		{
			OutPlan.bIsVariable = (*VariableValue)->AsBool();
		}

		if (const TSharedPtr<FJsonValue>* PropertiesValue = FindTypedField(Entry, TEXT("properties"), EJson::Object, Label, Problems))
		{
			OutPlan.Properties = (*PropertiesValue)->AsObject();
			CheckValues(OutPlan.Class->GetDefaultObject(), *OutPlan.Properties, Label + TEXT(" properties"), /*bCheckAsInstance=*/true, Problems);
		}

		if (const TSharedPtr<FJsonValue>* SlotValue = FindTypedField(Entry, TEXT("slot"), EJson::Object, Label, Problems))
		{
			const UClass* SlotClass = ParentClass ? ParentClass->GetDefaultObject<UPanelWidget>()->GetSlotClass() : nullptr;
			if (!SlotClass)
			{
				Problems.Add(FString::Printf(TEXT("%s: the root widget has no slot, so it cannot have 'slot' values"), *Label), TEXT("INVALID_ARGUMENT"));
			}
			else
			{
				OutPlan.Slot = (*SlotValue)->AsObject();
				CheckValues(SlotClass->GetDefaultObject(), *OutPlan.Slot, FString::Printf(TEXT("%s slot (%s)"), *Label, *SlotClass->GetName()), /*bCheckAsInstance=*/true, Problems);
			}
		}

		if (const TSharedPtr<FJsonValue>* ChildrenValue = FindTypedField(Entry, TEXT("children"), EJson::Array, Label, Problems))
		{
			const TArray<TSharedPtr<FJsonValue>>& ChildValues = (*ChildrenValue)->AsArray();
			const UPanelWidget* PanelDefaults = Cast<UPanelWidget>(OutPlan.Class->GetDefaultObject());
			if (!PanelDefaults)
			{
				if (!ChildValues.IsEmpty())
				{
					Problems.Add(FString::Printf(TEXT("%s: a %s is not a panel and cannot have children"), *Label, *OutPlan.Class->GetName()), TEXT("INVALID_ARGUMENT"));
				}
			}
			else if (!PanelDefaults->CanHaveMultipleChildren() && ChildValues.Num() > 1)
			{
				Problems.Add(FString::Printf(TEXT("%s: a %s holds a single child widget, not %d; put the children in a box panel inside it"),
					*Label, *OutPlan.Class->GetName(), ChildValues.Num()), TEXT("INVALID_ARGUMENT"));
			}
			else
			{
				OutPlan.Children.SetNum(ChildValues.Num());
				for (int32 ChildIndex = 0; ChildIndex < ChildValues.Num(); ++ChildIndex)
				{
					const FString ChildWhere = FString::Printf(TEXT("%s.children[%d]"), *Where, ChildIndex);
					const TSharedPtr<FJsonObject>* ChildEntry = nullptr;
					if (!ChildValues[ChildIndex].IsValid() || ChildValues[ChildIndex]->Type != EJson::Object || !ChildValues[ChildIndex]->TryGetObject(ChildEntry))
					{
						Problems.Add(FString::Printf(TEXT("%s must be an object"), *ChildWhere), TEXT("INVALID_ARGUMENT"));
						continue;
					}
					PlanWidget(Context, **ChildEntry, OutPlan.Class, ChildWhere, OutPlan.Children[ChildIndex]);
				}
			}
		}
	}

	/** Creates a planned widget under ParentPanel (as the root widget when null), then its children. Raises a tool error on failure. */
	UWidget* CreatePlannedWidget(UWidgetTree* WidgetTree, const FWidgetPlan& Plan, UPanelWidget* ParentPanel, int32 InsertIndex, int32 Depth, FAgentMcpWidgetEditResult& Result)
	{
		ReleaseStaleWidgetName(WidgetTree, Plan.Name);
		UWidget* Widget = Plan.Class->IsChildOf(UUserWidget::StaticClass())
			? static_cast<UWidget*>(WidgetTree->ConstructWidget<UUserWidget>(TSubclassOf<UUserWidget>(Plan.Class), Plan.Name))
			: WidgetTree->ConstructWidget<UWidget>(TSubclassOf<UWidget>(Plan.Class), Plan.Name);
		if (!Widget)
		{
			RaiseToolError(TEXT("WIDGET_CREATE_FAILED"), FString::Printf(TEXT("A %s named '%s' could not be created."), *Plan.Class->GetName(), *Plan.Name.ToString()));
			return nullptr;
		}
		Widget->SetFlags(RF_Transactional);
		// Designer defaults, as for a widget dragged from the palette; then the requested variable state.
		Widget->CreatedFromPalette();
		Widget->bIsVariable = Plan.bIsVariable;

		if (ParentPanel)
		{
			const UPanelSlot* AddedSlot = InsertIndex != INDEX_NONE ? ParentPanel->InsertChildAt(InsertIndex, Widget) : ParentPanel->AddChild(Widget);
			if (!AddedSlot)
			{
				RaiseToolError(TEXT("WIDGET_ADD_FAILED"), FString::Printf(TEXT("'%s' could not be added to '%s'."), *Widget->GetName(), *ParentPanel->GetName()));
				return nullptr;
			}
		}
		else
		{
			WidgetTree->RootWidget = Widget;
		}

		const FString Label = FString::Printf(TEXT("'%s'"), *Widget->GetName());
		bool bChanged = false;
		if (Plan.Properties.IsValid() && !ApplyValues(Widget, *Plan.Properties, Label, bChanged))
		{
			return nullptr;
		}
		if (Plan.Slot.IsValid() && !ApplyValues(Widget->Slot, *Plan.Slot, Label + TEXT(" slot"), bChanged))
		{
			return nullptr;
		}

		FAgentMcpWidgetNode& Node = Result.Widgets.Add_GetRef(MakeEditedWidgetNode(Widget, WidgetTree, Result.Widgets.Num(), Depth));
		if (Plan.Properties.IsValid())
		{
			Node.Properties.JsonObject = ReadRequestedValues(Widget, *Plan.Properties);
		}
		if (Plan.Slot.IsValid() && Widget->Slot)
		{
			Node.Slot.Properties.JsonObject = ReadRequestedValues(Widget->Slot, *Plan.Slot);
		}

		if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (const FWidgetPlan& Child : Plan.Children)
			{
				if (!CreatePlannedWidget(WidgetTree, Child, Panel, INDEX_NONE, Depth + 1, Result))
				{
					return nullptr;
				}
			}
		}
		return Widget;
	}
}

FAgentMcpWidgetBlueprintDetails UAgentMcpUmgTools::Inspect(UWidgetBlueprint* WidgetBlueprint, const FString& RootWidget, int32 MaxDepth, bool bIncludeSlots, bool bIncludeProperties, int32 MaxWidgets)
{
	using namespace UE::AgentMcp::UmgToolsPrivate;

	FAgentMcpWidgetBlueprintDetails Details;
	if (!UE::AgentMcp::Tools::RequireObject(WidgetBlueprint, TEXT("widgetBlueprint")))
	{
		return Details;
	}

	const UClass* ParentClass = WidgetBlueprint->ParentClass;
	Details.WidgetBlueprint = WidgetBlueprint->GetPathName();
	Details.ParentClass = GetPathNameSafe(ParentClass);
	Details.NativeParent = UE::AgentMcp::Tools::MakeNativeClassInfo(UE::AgentMcp::Tools::FindNativeClass(ParentClass));

	const UWidgetTree* WidgetTree = WidgetBlueprint->WidgetTree;
	if (!WidgetTree)
	{
		UE::AgentMcp::RaiseToolError(TEXT("NOT_SUPPORTED"), FString::Printf(TEXT("%s has no widget tree."), *Details.WidgetBlueprint));
		return Details;
	}

	TArray<UWidget*> AllWidgets;
	WidgetTree->GetAllWidgets(AllWidgets);
	Details.WidgetCount = AllWidgets.Num();

	const UWidget* StartWidget = WidgetTree->RootWidget;
	const FString StartName = RootWidget.TrimStartAndEnd();
	if (!StartName.IsEmpty())
	{
		StartWidget = StartName.Len() < NAME_SIZE ? WidgetTree->FindWidget(FName(*StartName)) : nullptr;
		if (!StartWidget)
		{
			UE::AgentMcp::RaiseToolError(TEXT("NOT_FOUND"), FString::Printf(TEXT("%s has no widget named '%s'."), *Details.WidgetBlueprint, *StartName.Left(128)),
				TEXT("Omit rootWidget to start at the root, or use a name from widgets[].name."));
			return Details;
		}
	}

	FWalkContext Context{ Details };
	Context.MaxDepth = FMath::Clamp(MaxDepth, 0, MaxTreeDepth);
	Context.MaxWidgets = FMath::Clamp(MaxWidgets, 1, MaxListedWidgets);
	Context.bIncludeSlots = bIncludeSlots;
	Context.bIncludeProperties = bIncludeProperties;
	Walk(Context, StartWidget, 0, FString(), NAME_None);

	CollectBindWidgets(WidgetBlueprint, &Details.BindWidgets, Details.MissingBindWidgets);

	for (const UWidgetAnimation* Animation : WidgetBlueprint->Animations)
	{
		if (Animation)
		{
			Details.Animations.Add(Animation->GetName());
		}
	}

	for (const FDelegateEditorBinding& Binding : WidgetBlueprint->Bindings)
	{
		FAgentMcpPropertyBinding& Entry = Details.PropertyBindings.AddDefaulted_GetRef();
		Entry.Widget = Binding.ObjectName;
		Entry.Property = Binding.PropertyName.ToString();
		Entry.Source = !Binding.FunctionName.IsNone() ? Binding.FunctionName.ToString() : Binding.SourceProperty.ToString();
	}

	return Details;
}

FAgentMcpWidgetBlueprintCreateResult UAgentMcpUmgTools::CreateWidgetBlueprint(const FString& AssetPath, TSubclassOf<UUserWidget> ParentClass, TSubclassOf<UPanelWidget> RootWidgetClass)
{
	using namespace UE::AgentMcp::UmgToolsPrivate;

	FAgentMcpWidgetBlueprintCreateResult Result;
	// Control tools are not blocked during PIE by the dispatcher, but new assets belong to the editor session.
	if (UE::AgentMcp::Tools::IsPlaySessionActive())
	{
		UE::AgentMcp::RaiseToolError(TEXT("PIE_ACTIVE"), TEXT("umg_create_widget_blueprint creates an asset and is blocked while a play session is running."),
			TEXT("Stop the play session first (pie_stop)."));
		return Result;
	}

	// The object path (/Game/UI/WBP_Hud.WBP_Hud) is accepted as well as the package path.
	FString PackageName;
	FString AssetName;
	FString PathCode;
	const FString PathProblem = UE::AgentMcp::Tools::GetNewAssetPathProblem(AssetPath, PackageName, AssetName, PathCode);
	if (!PathProblem.IsEmpty())
	{
		UE::AgentMcp::RaiseToolError(PathCode, PathProblem + TEXT("."), TEXT("Pass a package path under /Game or a project plugin, for example /Game/UI/WBP_Hud."));
		return Result;
	}
	// UAssetToolsImpl::CreateAsset would open an overwrite dialog for an existing asset, so check first.
	if (UE::AgentMcp::Tools::DoesAssetExist(PackageName, AssetName))
	{
		UE::AgentMcp::RaiseToolError(TEXT("ASSET_EXISTS"), FString::Printf(TEXT("An asset already exists at %s."), *PackageName),
			TEXT("Choose another path, or change the existing Widget Blueprint with umg_add_widgets."));
		return Result;
	}

	// UWidgetBlueprintFactory opens a message dialog for a parent class it cannot use, so check first.
	UClass* Parent = ParentClass.Get() ? ParentClass.Get() : UUserWidget::StaticClass();
	if (!Parent->IsChildOf(UUserWidget::StaticClass()) || !FKismetEditorUtilities::CanCreateBlueprintOfClass(Parent))
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("A Widget Blueprint cannot derive from %s."), *Parent->GetName()),
			TEXT("Use UserWidget or a Blueprintable subclass of it."));
		return Result;
	}
	UClass* RootClass = RootWidgetClass.Get();
	if (RootClass && RootClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("%s is abstract or deprecated and cannot be the root widget."), *RootClass->GetName()),
			TEXT("Use a panel such as CanvasPanel, Overlay or VerticalBox."));
		return Result;
	}

	UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
	Factory->BlueprintType = BPTYPE_Normal;
	Factory->ParentClass = Parent;
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(AssetTools.CreateAsset(AssetName, FPackageName::GetLongPackagePath(PackageName), UWidgetBlueprint::StaticClass(), Factory));
	if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
	{
		UE::AgentMcp::RaiseToolError(TEXT("ASSET_CREATE_FAILED"), FString::Printf(TEXT("The Widget Blueprint %s could not be created."), *PackageName),
			TEXT("log_get_recent may show the reason."));
		return Result;
	}

	UWidgetTree* WidgetTree = WidgetBlueprint->WidgetTree;
	if (RootClass && (!WidgetTree->RootWidget || WidgetTree->RootWidget->GetClass() != RootClass))
	{
		// The project setting DefaultRootWidget can give new Widget Blueprints a root of another class.
		if (UWidget* ProjectRoot = WidgetTree->RootWidget)
		{
			WidgetTree->RootWidget = nullptr;
			ProjectRoot->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
		}
		WidgetTree->RootWidget = WidgetTree->ConstructWidget<UWidget>(RootClass);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
	}

	Result.WidgetBlueprint = WidgetBlueprint->GetPathName();
	Result.ParentClass = GetPathNameSafe(WidgetBlueprint->ParentClass);
	if (const UWidget* Root = WidgetTree->RootWidget)
	{
		Result.RootWidget = Root->GetName();
		Result.RootWidgetClass = Root->GetClass()->GetName();
	}
	CollectBindWidgets(WidgetBlueprint, nullptr, Result.MissingBindWidgets);
	return Result;
}

FAgentMcpWidgetEditResult UAgentMcpUmgTools::AddWidgets(UWidgetBlueprint* WidgetBlueprint, const TArray<FJsonObjectWrapper>& Widgets, const FString& Parent, int32 Index)
{
	using namespace UE::AgentMcp::UmgToolsPrivate;

	FAgentMcpWidgetEditResult Result;
	UWidgetTree* WidgetTree = RequireEditableWidgetTree(WidgetBlueprint, Result.WidgetBlueprint);
	if (!WidgetTree)
	{
		return Result;
	}
	if (Widgets.IsEmpty())
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("'widgets' must contain at least one widget entry."),
			TEXT("An entry looks like {\"class\": \"TextBlock\", \"name\": \"Title\"}."));
		return Result;
	}

	FAddContext Context(WidgetBlueprint);
	FProblemList& Problems = Context.Problems;
	UPanelWidget* ParentPanel = nullptr;
	const FString ParentName = Parent.TrimStartAndEnd();
	if (ParentName.IsEmpty())
	{
		if (const UWidget* Root = WidgetTree->RootWidget)
		{
			Problems.Add(FString::Printf(TEXT("the tree already has the root widget '%s'; pass it or another panel as 'parent'"), *Root->GetName()), TEXT("INVALID_ARGUMENT"));
		}
		else if (Widgets.Num() != 1)
		{
			Problems.Add(TEXT("a widget tree has one root widget, so add one entry and give it the other widgets as children"), TEXT("INVALID_ARGUMENT"));
		}
		if (Index != INDEX_NONE)
		{
			Problems.Add(TEXT("'index' needs a 'parent' panel"), TEXT("INVALID_ARGUMENT"));
		}
	}
	else if (UWidget* ParentWidget = FindOwnWidget(WidgetBlueprint, ParentName))
	{
		ParentPanel = Cast<UPanelWidget>(ParentWidget);
		if (!ParentPanel)
		{
			Problems.Add(FString::Printf(TEXT("the parent '%s' is a %s, which cannot have child widgets"), *ParentWidget->GetName(), *ParentWidget->GetClass()->GetName()), TEXT("INVALID_ARGUMENT"));
		}
		else
		{
			const int32 ChildCount = ParentPanel->GetChildrenCount();
			if (!ParentPanel->CanHaveMultipleChildren() && ChildCount + Widgets.Num() > 1)
			{
				Problems.Add(FString::Printf(TEXT("the parent '%s' is a %s, which holds a single child widget%s"), *ParentPanel->GetName(), *ParentPanel->GetClass()->GetName(),
					ChildCount > 0 ? TEXT(" and already has one") : TEXT("")), TEXT("INVALID_ARGUMENT"));
			}
			if (Index < INDEX_NONE || Index > ChildCount)
			{
				Problems.Add(FString::Printf(TEXT("'index' %d is outside 0..%d for '%s'; -1 appends"), Index, ChildCount, *ParentPanel->GetName()), TEXT("INVALID_ARGUMENT"));
			}
		}
	}
	else
	{
		Problems.Add(DescribeMissingWidget(WidgetBlueprint, ParentName), TEXT("NOT_FOUND"));
	}

	// Entries are checked for a valid parent only, so that their slot values are checked against the right slot class.
	TArray<FWidgetPlan> Plans;
	if (Problems.IsEmpty())
	{
		Plans.SetNum(Widgets.Num());
		const UClass* ParentClass = ParentPanel ? ParentPanel->GetClass() : nullptr;
		for (int32 EntryIndex = 0; EntryIndex < Widgets.Num(); ++EntryIndex)
		{
			const FString Where = FString::Printf(TEXT("widgets[%d]"), EntryIndex);
			if (!Widgets[EntryIndex].JsonObject.IsValid())
			{
				Problems.Add(FString::Printf(TEXT("%s must be an object"), *Where), TEXT("INVALID_ARGUMENT"));
				continue;
			}
			PlanWidget(Context, *Widgets[EntryIndex].JsonObject, ParentClass, Where, Plans[EntryIndex]);
		}
	}
	if (!Problems.IsEmpty())
	{
		UE::AgentMcp::Tools::RaiseProblems(FString::Printf(TEXT("Nothing was added to %s"), *WidgetBlueprint->GetName()), Problems.Messages, Problems.Codes,
			TEXT("umg_inspect lists the widgets; object_list_properties on /Script/UMG.Default__TextBlock or /Script/UMG.Default__CanvasPanelSlot lists property names and types."));
		return Result;
	}

	// Record the tree and the parent for undo, as the designer does when a widget is dropped.
	WidgetTree->SetFlags(RF_Transactional);
	WidgetTree->Modify();
	if (ParentPanel)
	{
		ParentPanel->SetFlags(RF_Transactional);
		ParentPanel->Modify();
	}

	int32 InsertIndex = Index;
	for (const FWidgetPlan& Plan : Plans)
	{
		if (!CreatePlannedWidget(WidgetTree, Plan, ParentPanel, InsertIndex, 0, Result))
		{
			return Result;
		}
		if (InsertIndex != INDEX_NONE)
		{
			++InsertIndex;
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
	Result.bApplied = true;
	CollectBindWidgets(WidgetBlueprint, nullptr, Result.MissingBindWidgets);
	return Result;
}

FAgentMcpWidgetEditResult UAgentMcpUmgTools::SetWidgetProperties(UWidgetBlueprint* WidgetBlueprint, const FJsonObjectWrapper& Widgets)
{
	using namespace UE::AgentMcp::UmgToolsPrivate;

	FAgentMcpWidgetEditResult Result;
	UWidgetTree* WidgetTree = RequireEditableWidgetTree(WidgetBlueprint, Result.WidgetBlueprint);
	if (!WidgetTree)
	{
		return Result;
	}

	const TSharedPtr<FJsonObject>& Requested = Widgets.JsonObject;
	if (!Requested.IsValid() || Requested->Values.IsEmpty())
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("'widgets' must name at least one widget."),
			TEXT("For example {\"Title\": {\"properties\": {\"Text\": \"Hello\"}}}."));
		return Result;
	}

	struct FWidgetEdit
	{
		UWidget* Widget = nullptr;
		TSharedPtr<FJsonObject> Properties;
		TSharedPtr<FJsonObject> Slot;
		TOptional<bool> bIsVariable;
	};

	// Check every change before changing anything, so a bad value leaves the Widget Blueprint untouched.
	TArray<FWidgetEdit> Edits;
	FProblemList Problems;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Requested->Values)
	{
		const FString Label = FString::Printf(TEXT("'%s'"), *Pair.Key.Left(128));
		UWidget* Widget = FindOwnWidget(WidgetBlueprint, Pair.Key);
		if (!Widget)
		{
			Problems.Add(DescribeMissingWidget(WidgetBlueprint, Pair.Key), TEXT("NOT_FOUND"));
			continue;
		}
		if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Object)
		{
			Problems.Add(FString::Printf(TEXT("%s must map to an object with properties, slot or isVariable"), *Label), TEXT("INVALID_ARGUMENT"));
			continue;
		}

		const FJsonObject& Changes = *Pair.Value->AsObject();
		ReportUnknownFields(Changes, { TEXT("properties"), TEXT("slot"), TEXT("isVariable") }, Label, Problems);
		if (Changes.Values.IsEmpty())
		{
			Problems.Add(FString::Printf(TEXT("%s lists no changes"), *Label), TEXT("INVALID_ARGUMENT"));
		}

		FWidgetEdit& Edit = Edits.AddDefaulted_GetRef();
		Edit.Widget = Widget;
		if (const TSharedPtr<FJsonValue>* PropertiesValue = FindTypedField(Changes, TEXT("properties"), EJson::Object, Label, Problems))
		{
			Edit.Properties = (*PropertiesValue)->AsObject();
			CheckValues(Widget, *Edit.Properties, Label + TEXT(" properties"), /*bCheckAsInstance=*/false, Problems);
		}
		if (const TSharedPtr<FJsonValue>* SlotValue = FindTypedField(Changes, TEXT("slot"), EJson::Object, Label, Problems))
		{
			if (!Widget->Slot)
			{
				Problems.Add(FString::Printf(TEXT("%s is the root widget and has no slot"), *Label), TEXT("INVALID_ARGUMENT"));
			}
			else
			{
				Edit.Slot = (*SlotValue)->AsObject();
				CheckValues(Widget->Slot, *Edit.Slot, FString::Printf(TEXT("%s slot (%s)"), *Label, *Widget->Slot->GetClass()->GetName()), /*bCheckAsInstance=*/false, Problems);
			}
		}
		if (const TSharedPtr<FJsonValue>* VariableValue = FindTypedField(Changes, TEXT("isVariable"), EJson::Boolean, Label, Problems))
		{
			Edit.bIsVariable = (*VariableValue)->AsBool();
		}
	}
	if (!Problems.IsEmpty())
	{
		UE::AgentMcp::Tools::RaiseProblems(FString::Printf(TEXT("Nothing was changed in %s"), *WidgetBlueprint->GetName()), Problems.Messages, Problems.Codes,
			TEXT("umg_inspect lists the widgets; object_list_properties on /Script/UMG.Default__TextBlock or /Script/UMG.Default__CanvasPanelSlot lists property names and types."));
		return Result;
	}

	bool bChanged = false;
	bool bStructural = false;
	for (const FWidgetEdit& Edit : Edits)
	{
		const FString Label = FString::Printf(TEXT("'%s'"), *Edit.Widget->GetName());
		if (Edit.Properties.IsValid() && !ApplyValues(Edit.Widget, *Edit.Properties, Label, bChanged))
		{
			return Result;
		}
		if (Edit.Slot.IsValid() && !ApplyValues(Edit.Widget->Slot, *Edit.Slot, Label + TEXT(" slot"), bChanged))
		{
			return Result;
		}
		if (Edit.bIsVariable.IsSet() && static_cast<bool>(Edit.Widget->bIsVariable) != Edit.bIsVariable.GetValue())
		{
			Edit.Widget->SetFlags(RF_Transactional);
			Edit.Widget->Modify();
			Edit.Widget->bIsVariable = Edit.bIsVariable.GetValue();
			bChanged = true;
			bStructural = true;
		}

		FAgentMcpWidgetNode& Node = Result.Widgets.Add_GetRef(MakeEditedWidgetNode(Edit.Widget, WidgetTree, Result.Widgets.Num(), 0));
		if (Edit.Properties.IsValid())
		{
			Node.Properties.JsonObject = ReadRequestedValues(Edit.Widget, *Edit.Properties);
		}
		if (Edit.Slot.IsValid())
		{
			Node.Slot.Properties.JsonObject = ReadRequestedValues(Edit.Widget->Slot, *Edit.Slot);
		}
	}

	if (bStructural)
	{
		// Variables are members of the generated class.
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
	}
	else if (bChanged)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBlueprint);
	}
	Result.bApplied = true;
	CollectBindWidgets(WidgetBlueprint, nullptr, Result.MissingBindWidgets);
	return Result;
}

FAgentMcpWidgetEditResult UAgentMcpUmgTools::RemoveWidgets(UWidgetBlueprint* WidgetBlueprint, const TArray<FString>& WidgetNames, bool bConfirm)
{
	using namespace UE::AgentMcp::UmgToolsPrivate;

	FAgentMcpWidgetEditResult Result;
	UWidgetTree* WidgetTree = RequireEditableWidgetTree(WidgetBlueprint, Result.WidgetBlueprint);
	if (!WidgetTree)
	{
		return Result;
	}
	if (WidgetNames.IsEmpty())
	{
		UE::AgentMcp::RaiseToolError(TEXT("INVALID_ARGUMENT"), TEXT("'widgetNames' must name at least one widget."), TEXT("umg_inspect lists the widgets."));
		return Result;
	}

	FProblemList Problems;
	TArray<UWidget*> Requested;
	for (const FString& Name : WidgetNames)
	{
		if (UWidget* Widget = FindOwnWidget(WidgetBlueprint, Name))
		{
			Requested.AddUnique(Widget);
		}
		else
		{
			Problems.Add(DescribeMissingWidget(WidgetBlueprint, Name), TEXT("NOT_FOUND"));
		}
	}
	if (!Problems.IsEmpty())
	{
		UE::AgentMcp::Tools::RaiseProblems(FString::Printf(TEXT("Nothing was removed from %s"), *WidgetBlueprint->GetName()), Problems.Messages, Problems.Codes,
			TEXT("umg_inspect lists the widgets."));
		return Result;
	}

	// A requested widget inside another requested widget is removed with it.
	const TSet<UWidget*> RequestedSet(Requested);
	TArray<UWidget*> Roots;
	for (UWidget* Widget : Requested)
	{
		bool bInsideRequested = false;
		int32 Steps = 0;
		for (UWidget* Container = GetContainer(Widget, WidgetTree); Container && Steps < MaxSearchSteps; Container = GetContainer(Container, WidgetTree), ++Steps)
		{
			if (RequestedSet.Contains(Container))
			{
				bInsideRequested = true;
				break;
			}
		}
		if (!bInsideRequested)
		{
			Roots.Add(Widget);
		}
	}

	// List the widgets in tree order and collect each subtree before anything changes.
	FAgentMcpWidgetBlueprintDetails Listing;
	FWalkContext WalkContext{ Listing };
	WalkContext.MaxDepth = MaxTreeDepth;
	WalkContext.MaxWidgets = MaxListedWidgets;
	WalkContext.bIncludeSlots = false;
	TArray<TArray<UWidget*>> Subtrees;
	for (UWidget* Root : Roots)
	{
		const UWidget* Container = GetContainer(Root, WidgetTree);
		Walk(WalkContext, Root, 0, Container ? Container->GetName() : FString(), NAME_None);

		TArray<UWidget*> Descendants;
		UWidgetTree::GetChildWidgets(Root, Descendants);
		TArray<UWidget*>& Subtree = Subtrees.AddDefaulted_GetRef();
		Subtree.Add(Root);
		Subtree.Append(Descendants);
	}
	Result.Widgets = MoveTemp(Listing.Widgets);

	const UClass* ParentClass = WidgetBlueprint->ParentClass;
	for (const TArray<UWidget*>& Subtree : Subtrees)
	{
		for (const UWidget* Widget : Subtree)
		{
			const FString WidgetName = Widget->GetName();
			const FName WidgetFName = Widget->GetFName();
			const FObjectPropertyBase* Property = ParentClass ? CastField<FObjectPropertyBase>(ParentClass->FindPropertyByName(WidgetFName)) : nullptr;
			bool bOptional = false;
			if (Property && FWidgetBlueprintEditorUtils::IsBindWidgetProperty(Property, bOptional) && !bOptional)
			{
				Result.Warnings.Add(FString::Printf(TEXT("'%s' satisfies the required BindWidget property %s.%s; blueprint_compile fails until a widget of that name exists again."),
					*WidgetName, *GetNameSafe(Property->GetOwnerClass()), *WidgetName));
			}
			if (FBlueprintEditorUtils::IsVariableUsed(WidgetBlueprint, WidgetFName))
			{
				Result.Warnings.Add(FString::Printf(TEXT("Graph nodes use the variable '%s'; they are removed with the widget."), *WidgetName));
			}
			for (const FDelegateEditorBinding& Binding : WidgetBlueprint->Bindings)
			{
				if (Binding.ObjectName == WidgetName)
				{
					Result.Warnings.Add(FString::Printf(TEXT("The property binding of %s.%s is removed."), *WidgetName, *Binding.PropertyName.ToString()));
				}
			}
			for (const UWidgetAnimation* Animation : WidgetBlueprint->Animations)
			{
				if (Animation && Animation->GetBindings().ContainsByPredicate([WidgetFName](const FWidgetAnimationBinding& Binding) { return Binding.WidgetName == WidgetFName; }))
				{
					Result.Warnings.Add(FString::Printf(TEXT("Animation '%s' has tracks for '%s'; they no longer affect a widget."), *Animation->GetName(), *WidgetName));
				}
			}
		}
	}

	if (!bConfirm)
	{
		Result.Warnings.Add(TEXT("Dry run: nothing was removed. Call again with bConfirm true to remove these widgets."));
		CollectBindWidgets(WidgetBlueprint, nullptr, Result.MissingBindWidgets);
		return Result;
	}

	// Follows FWidgetBlueprintEditorUtils::DeleteWidgets, which needs an open Widget Blueprint editor.
	WidgetTree->SetFlags(RF_Transactional);
	WidgetTree->Modify();
	WidgetBlueprint->Modify();

	const UClass* GeneratedClass = WidgetBlueprint->GeneratedClass;
	UUserWidget* WidgetDefaults = GeneratedClass ? Cast<UUserWidget>(GeneratedClass->GetDefaultObject()) : nullptr;
	for (int32 RootIndex = 0; RootIndex < Roots.Num(); ++RootIndex)
	{
		UWidget* Root = Roots[RootIndex];
		Root->SetFlags(RF_Transactional);
		if (UPanelWidget* ParentPanel = Root->GetParent())
		{
			ParentPanel->SetFlags(RF_Transactional);
			ParentPanel->Modify();
		}
		Root->Modify();

		bool bRemoved = WidgetTree->RemoveWidget(Root);
		if (!bRemoved && !Root->GetParent())
		{
			// Content of a named slot of a nested user widget.
			const TScriptInterface<INamedSlotInterface> SlotHost = FWidgetBlueprintEditorUtils::FindNamedSlotHostForContent(Root, WidgetTree);
			if (SlotHost.GetObject())
			{
				bRemoved = FWidgetBlueprintEditorUtils::RemoveNamedSlotHostContent(Root, SlotHost);
			}
		}
		if (!bRemoved)
		{
			UE::AgentMcp::RaiseToolError(TEXT("WIDGET_REMOVE_FAILED"), FString::Printf(TEXT("'%s' could not be removed from %s."), *Root->GetName(), *WidgetBlueprint->GetName()));
			return Result;
		}

		for (UWidget* Widget : Subtrees[RootIndex])
		{
			const FString WidgetName = Widget->GetName();
			const FName WidgetFName = Widget->GetFName();
			WidgetBlueprint->Bindings.RemoveAll([&WidgetName](const FDelegateEditorBinding& Binding) { return Binding.ObjectName == WidgetName; });
			if (FBlueprintEditorUtils::IsVariableUsed(WidgetBlueprint, WidgetFName))
			{
				FBlueprintEditorUtils::RemoveVariableNodes(WidgetBlueprint, WidgetFName);
			}
			if (WidgetDefaults && WidgetDefaults->GetDesiredFocusWidgetName() == WidgetFName)
			{
				WidgetDefaults->SetFlags(RF_Transactional);
				WidgetDefaults->Modify();
				WidgetDefaults->SetDesiredFocusWidget(NAME_None);
			}
		}

		// Move the removed widgets to the transient package so that new widgets can take their names, as the designer does.
		for (UWidget* Widget : Subtrees[RootIndex])
		{
			Widget->SetFlags(RF_Transactional);
			Widget->Modify();
			Widget->Rename(nullptr, GetTransientPackage());
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
	Result.bApplied = true;
	CollectBindWidgets(WidgetBlueprint, nullptr, Result.MissingBindWidgets);
	return Result;
}
