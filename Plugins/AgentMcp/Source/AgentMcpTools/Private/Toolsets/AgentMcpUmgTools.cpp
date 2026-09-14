#include "AgentMcpUmgTools.h"

#include "AgentMcpJson.h"
#include "AgentMcpToolsCommon.h"

#include "Animation/WidgetAnimation.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/NamedSlotInterface.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditorUtils.h"

namespace UE::AgentMcp::UmgToolsPrivate
{
	constexpr int32 MaxTreeDepth = 64;
	constexpr int32 MaxListedWidgets = 2000;
	constexpr int32 MaxPropertiesPerObject = 40;

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

	if (ParentClass)
	{
		for (TFieldIterator<FObjectPropertyBase> It(ParentClass); It; ++It)
		{
			const FObjectPropertyBase* Property = *It;
			bool bOptional = false;
			if (!FWidgetBlueprintEditorUtils::IsBindWidgetProperty(Property, bOptional))
			{
				continue;
			}

			FAgentMcpBindWidget& Binding = Details.BindWidgets.AddDefaulted_GetRef();
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
				Details.MissingBindWidgets.Add(Binding.Property);
			}
		}
	}

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
