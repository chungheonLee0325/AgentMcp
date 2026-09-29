#include "AgentMcpAnimationTools.h"

#include "AgentMcpToolsCommon.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_Inertialization.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_Slot.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/Skeleton.h"
#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Factories/AnimMontageFactory.h"
#include "Factories/BlendSpaceFactory1D.h"
#include "IAssetTools.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/SoftObjectPath.h"

namespace UE::AgentMcp::AnimationToolsPrivate
{
	/** An asset by object path (/Game/A/B.B) or package path (/Game/A/B). */
	template <typename T>
	T* LoadAsset(const FString& Path)
	{
		if (Path.IsEmpty())
		{
			return nullptr;
		}
		const FString ObjectPath = Path.Contains(TEXT(".")) ? Path : Path + TEXT(".") + FPackageName::GetShortName(Path);
		return Cast<T>(FSoftObjectPath(ObjectPath).TryLoad());
	}

	bool UsesSkeleton(const UAnimationAsset* Asset, const USkeleton* Skeleton)
	{
		const USkeleton* AssetSkeleton = Asset ? Asset->GetSkeleton() : nullptr;
		return AssetSkeleton && Skeleton && (AssetSkeleton == Skeleton || Skeleton->IsCompatibleForEditor(AssetSkeleton));
	}

	/**
	 * Checks the path of the asset to build and finds an asset of type T already there. Raises the error and returns false when the path
	 * is bad, a play session runs, the path holds another kind of asset, or it holds one and bReplace is false.
	 */
	template <typename T>
	bool PrepareTarget(const TCHAR* ToolName, const FString& AssetPath, bool bReplace, FString& OutPackageName, FString& OutAssetName, T*& OutExisting)
	{
		OutExisting = nullptr;
		if (Tools::IsPlaySessionActive())
		{
			RaiseToolError(TEXT("PIE_ACTIVE"), FString::Printf(TEXT("%s changes an asset and is blocked while a play session is running."), ToolName),
				TEXT("Stop the play session first (pie_stop)."));
			return false;
		}
		FString Code;
		const FString Problem = Tools::GetNewAssetPathProblem(AssetPath, OutPackageName, OutAssetName, Code);
		if (!Problem.IsEmpty())
		{
			RaiseToolError(Code, Problem + TEXT("."), TEXT("Pass a package path under /Game or a project plugin, for example /Game/Anim/BS_Locomotion."));
			return false;
		}
		if (!Tools::DoesAssetExist(OutPackageName, OutAssetName))
		{
			return true;
		}
		UObject* Existing = LoadAsset<UObject>(OutPackageName + TEXT(".") + OutAssetName);
		OutExisting = Cast<T>(Existing);
		if (!OutExisting)
		{
			RaiseToolError(TEXT("ASSET_EXISTS"), FString::Printf(TEXT("%s holds %s, not a %s."), *OutPackageName,
				Existing ? *FString::Printf(TEXT("a %s"), *Existing->GetClass()->GetName()) : TEXT("an asset that could not be loaded"), *T::StaticClass()->GetName()),
				TEXT("Choose another path."));
			return false;
		}
		if (!bReplace)
		{
			RaiseToolError(TEXT("ASSET_EXISTS"), FString::Printf(TEXT("A %s already exists at %s."), *T::StaticClass()->GetName(), *OutPackageName),
				TEXT("Pass bReplace true to rebuild it, or choose another path."));
			return false;
		}
		return true;
	}

	UObject* CreateAsset(const FString& PackageName, const FString& AssetName, UClass* Class, UFactory* Factory)
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		return AssetTools.CreateAsset(AssetName, FPackageName::GetLongPackagePath(PackageName), Class, Factory);
	}

	/** Raises INVALID_ARGUMENT with every problem when there is one; nothing has changed at that point. */
	bool ReportProblems(const TArray<FString>& Problems)
	{
		if (Problems.IsEmpty())
		{
			return false;
		}
		RaiseToolError(TEXT("INVALID_ARGUMENT"), FString::Join(Problems, TEXT("; ")) + TEXT("."), TEXT("Fix every listed problem; nothing was changed."));
		return true;
	}

	/** A number, or the name of a variable the node reads each frame. */
	struct FNodeInput
	{
		bool bSet = false;
		double Constant = 0.0;
		FName Variable;
	};

	/** One node of the anim graph tree, read and checked before anything changes. */
	struct FNodePlan
	{
		FString Type;
		FString Path;
		FName Slot;
		UBlendSpace* BlendSpace = nullptr;
		UAnimSequenceBase* Sequence = nullptr;
		bool bLoop = true;
		FNodeInput X;
		FNodeInput Y;
		TSharedPtr<FNodePlan> Source;
	};

	void ReadInput(const FJsonObject& Json, const TCHAR* Field, const FString& Path, bool bRequired, FNodeInput& OutInput, TArray<FString>& Problems)
	{
		const TSharedPtr<FJsonValue> Value = Json.TryGetField(Field);
		if (!Value.IsValid() || Value->Type == EJson::Null)
		{
			if (bRequired)
			{
				Problems.Add(FString::Printf(TEXT("%s.%s is missing: a number or a variable name"), *Path, Field));
			}
			return;
		}
		OutInput.bSet = true;
		if (Value->Type == EJson::Number)
		{
			OutInput.Constant = Value->AsNumber();
		}
		else if (Value->Type == EJson::String && !Value->AsString().IsEmpty())
		{
			OutInput.Variable = FName(*Value->AsString());
		}
		else
		{
			Problems.Add(FString::Printf(TEXT("%s.%s is neither a number nor a variable name"), *Path, Field));
		}
	}

	TSharedPtr<FNodePlan> PlanNode(const TSharedPtr<FJsonObject>& Json, const FString& Path, const USkeleton* Skeleton, TArray<FString>& Problems)
	{
		TSharedPtr<FNodePlan> Plan = MakeShared<FNodePlan>();
		Plan->Path = Path;
		if (!Json.IsValid())
		{
			Problems.Add(Path + TEXT(" is not an object"));
			return Plan;
		}
		Json->TryGetStringField(TEXT("node"), Plan->Type);
		if (Plan->Type == TEXT("Slot"))
		{
			FString SlotName = TEXT("DefaultSlot");
			Json->TryGetStringField(TEXT("slot"), SlotName);
			Plan->Slot = FName(*SlotName);
			const TSharedPtr<FJsonObject>* Source = nullptr;
			if (Json->TryGetObjectField(TEXT("source"), Source) && Source)
			{
				Plan->Source = PlanNode(*Source, Path + TEXT(".source"), Skeleton, Problems);
			}
			else
			{
				Problems.Add(Path + TEXT(".source is missing: a Slot plays montages over a source pose"));
			}
		}
		else if (Plan->Type == TEXT("Inertialization"))
		{
			const TSharedPtr<FJsonObject>* Source = nullptr;
			if (Json->TryGetObjectField(TEXT("source"), Source) && Source)
			{
				Plan->Source = PlanNode(*Source, Path + TEXT(".source"), Skeleton, Problems);
			}
			else
			{
				Problems.Add(Path + TEXT(".source is missing: Inertialization blends the pose of its source"));
			}
		}
		else if (Plan->Type == TEXT("BlendSpacePlayer"))
		{
			FString AssetPath;
			Json->TryGetStringField(TEXT("blendSpace"), AssetPath);
			Plan->BlendSpace = LoadAsset<UBlendSpace>(AssetPath);
			if (!Plan->BlendSpace)
			{
				Problems.Add(FString::Printf(TEXT("%s.blendSpace '%s' is not a blend space"), *Path, *AssetPath));
			}
			else if (!UsesSkeleton(Plan->BlendSpace, Skeleton))
			{
				Problems.Add(FString::Printf(TEXT("%s.blendSpace %s uses another skeleton"), *Path, *AssetPath));
			}
			ReadInput(*Json, TEXT("x"), Path, /*bRequired=*/true, Plan->X, Problems);
			ReadInput(*Json, TEXT("y"), Path, /*bRequired=*/false, Plan->Y, Problems);
		}
		else if (Plan->Type == TEXT("SequencePlayer"))
		{
			FString AssetPath;
			Json->TryGetStringField(TEXT("sequence"), AssetPath);
			Plan->Sequence = LoadAsset<UAnimSequenceBase>(AssetPath);
			if (!Plan->Sequence)
			{
				Problems.Add(FString::Printf(TEXT("%s.sequence '%s' is not an animation sequence"), *Path, *AssetPath));
			}
			else if (!UsesSkeleton(Plan->Sequence, Skeleton))
			{
				Problems.Add(FString::Printf(TEXT("%s.sequence %s uses another skeleton"), *Path, *AssetPath));
			}
			Json->TryGetBoolField(TEXT("loop"), Plan->bLoop);
		}
		else
		{
			Problems.Add(FString::Printf(TEXT("%s.node '%s' is not Slot, Inertialization, BlendSpacePlayer or SequencePlayer"), *Path, *Plan->Type));
		}
		return Plan;
	}

	void CollectVariables(const FNodePlan& Plan, TArray<TPair<FName, FString>>& OutUses)
	{
		for (const FNodeInput* Input : { &Plan.X, &Plan.Y })
		{
			if (Input->bSet && !Input->Variable.IsNone())
			{
				OutUses.Add({ Input->Variable, Plan.Path });
			}
		}
		if (Plan.Source.IsValid())
		{
			CollectVariables(*Plan.Source, OutUses);
		}
	}

	bool IsNumericProperty(const FProperty* Property)
	{
		return Property && Property->IsA<FNumericProperty>();
	}
}

FAgentMcpAnimAssetResult UAgentMcpAnimationTools::BuildBlendSpace(const FString& AssetPath, USkeleton* Skeleton, const FString& AxisName, double AxisMin, double AxisMax,
	const TArray<FAgentMcpBlendSample>& Samples, int32 GridDivisions, bool bReplace, double SmoothingSeconds)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::AnimationToolsPrivate;

	FAgentMcpAnimAssetResult Result;
	if (!Tools::RequireObject(Skeleton, TEXT("skeleton")))
	{
		return Result;
	}
	FString PackageName;
	FString AssetName;
	UBlendSpace1D* BlendSpace = nullptr;
	if (!PrepareTarget(TEXT("anim_build_blend_space"), AssetPath, bReplace, PackageName, AssetName, BlendSpace))
	{
		return Result;
	}

	TArray<FString> Problems;
	if (SmoothingSeconds < 0.0)
	{
		Problems.Add(TEXT("smoothingSeconds cannot be negative"));
	}
	if (AxisName.TrimStartAndEnd().IsEmpty())
	{
		Problems.Add(TEXT("axisName is empty"));
	}
	if (!(AxisMax > AxisMin))
	{
		Problems.Add(FString::Printf(TEXT("axisMax %g is not above axisMin %g"), AxisMax, AxisMin));
	}
	if (GridDivisions < 1 || GridDivisions > 64)
	{
		Problems.Add(FString::Printf(TEXT("gridDivisions %d is outside 1-64"), GridDivisions));
	}
	if (Samples.IsEmpty())
	{
		Problems.Add(TEXT("samples is empty"));
	}
	if (BlendSpace && BlendSpace->GetSkeleton() != Skeleton)
	{
		Problems.Add(FString::Printf(TEXT("the blend space at %s uses the skeleton %s"), *PackageName, *GetPathNameSafe(BlendSpace->GetSkeleton())));
	}
	TArray<UAnimSequence*> Animations;
	TSet<double> Values;
	for (int32 Index = 0; Index < Samples.Num(); ++Index)
	{
		const FAgentMcpBlendSample& Sample = Samples[Index];
		UAnimSequence* Animation = LoadAsset<UAnimSequence>(Sample.Animation);
		if (!Animation)
		{
			Problems.Add(FString::Printf(TEXT("samples[%d]: '%s' is not an animation sequence"), Index, *Sample.Animation));
		}
		else if (!UsesSkeleton(Animation, Skeleton))
		{
			Problems.Add(FString::Printf(TEXT("samples[%d]: %s uses another skeleton"), Index, *Sample.Animation));
		}
		if (Sample.Value < AxisMin || Sample.Value > AxisMax)
		{
			Problems.Add(FString::Printf(TEXT("samples[%d]: value %g is outside the axis"), Index, Sample.Value));
		}
		bool bRepeated = false;
		Values.Add(Sample.Value, &bRepeated);
		if (bRepeated)
		{
			Problems.Add(FString::Printf(TEXT("samples[%d]: value %g is taken by another sample"), Index, Sample.Value));
		}
		Animations.Add(Animation);
	}
	if (ReportProblems(Problems))
	{
		return Result;
	}

	if (!BlendSpace)
	{
		UBlendSpaceFactory1D* Factory = NewObject<UBlendSpaceFactory1D>();
		Factory->TargetSkeleton = Skeleton;
		BlendSpace = Cast<UBlendSpace1D>(CreateAsset(PackageName, AssetName, UBlendSpace1D::StaticClass(), Factory));
		if (!BlendSpace)
		{
			RaiseToolError(TEXT("ASSET_CREATE_FAILED"), FString::Printf(TEXT("The blend space %s could not be created."), *PackageName), TEXT("log_get_recent may show the reason."));
			return Result;
		}
		Result.bCreated = true;
	}

	BlendSpace->Modify();
	// The axis is a protected property that the details panel edits through reflection; the tool edits it the same way. The axis comes
	// first, because a sample is checked against it.
	const FProperty* ParametersProperty = UBlendSpace::StaticClass()->FindPropertyByName(TEXT("BlendParameters"));
	FBlendParameter* Parameters = ParametersProperty ? ParametersProperty->ContainerPtrToValuePtr<FBlendParameter>(BlendSpace) : nullptr;
	if (!Parameters)
	{
		RaiseToolError(TEXT("NOT_SUPPORTED"), TEXT("The axis of the blend space could not be found."));
		return Result;
	}
	Parameters[0].DisplayName = AxisName;
	Parameters[0].Min = float(AxisMin);
	Parameters[0].Max = float(AxisMax);
	Parameters[0].GridNum = GridDivisions;
	// The smoothing of the axis is protected the same way.
	const FProperty* InterpolationProperty = UBlendSpace::StaticClass()->FindPropertyByName(TEXT("InterpolationParam"));
	if (FInterpolationParameter* Interpolation = InterpolationProperty ? InterpolationProperty->ContainerPtrToValuePtr<FInterpolationParameter>(BlendSpace) : nullptr)
	{
		Interpolation[0].InterpolationTime = float(SmoothingSeconds);
	}
	for (int32 Index = BlendSpace->GetNumberOfBlendSamples() - 1; Index >= 0; --Index)
	{
		BlendSpace->DeleteSample(Index);
	}
	for (int32 Index = 0; Index < Samples.Num(); ++Index)
	{
		BlendSpace->AddSample(Animations[Index], FVector(Samples[Index].Value, 0.0, 0.0));
	}
	BlendSpace->ValidateSampleData();
	BlendSpace->ResampleData();
	BlendSpace->PostEditChange();
	BlendSpace->MarkPackageDirty();

	Result.Asset = BlendSpace->GetPathName();
	Result.SampleCount = BlendSpace->GetNumberOfBlendSamples();
	Result.bDirty = BlendSpace->GetPackage()->IsDirty();
	return Result;
}

FAgentMcpAnimAssetResult UAgentMcpAnimationTools::BuildMontage(const FString& AssetPath, USkeleton* Skeleton, const TArray<FAgentMcpMontageSegment>& Segments,
	const FString& SlotName, double BlendInSeconds, double BlendOutSeconds, bool bReplace)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::AnimationToolsPrivate;

	FAgentMcpAnimAssetResult Result;
	if (!Tools::RequireObject(Skeleton, TEXT("skeleton")))
	{
		return Result;
	}
	FString PackageName;
	FString AssetName;
	UAnimMontage* Montage = nullptr;
	if (!PrepareTarget(TEXT("anim_build_montage"), AssetPath, bReplace, PackageName, AssetName, Montage))
	{
		return Result;
	}

	TArray<FString> Problems;
	if (SlotName.TrimStartAndEnd().IsEmpty())
	{
		Problems.Add(TEXT("slotName is empty"));
	}
	if (BlendInSeconds < 0.0 || BlendOutSeconds < 0.0)
	{
		Problems.Add(TEXT("blend times cannot be negative"));
	}
	if (Segments.IsEmpty())
	{
		Problems.Add(TEXT("segments is empty"));
	}
	if (Montage && Montage->GetSkeleton() != Skeleton)
	{
		Problems.Add(FString::Printf(TEXT("the montage at %s uses the skeleton %s"), *PackageName, *GetPathNameSafe(Montage->GetSkeleton())));
	}
	TArray<UAnimSequence*> Animations;
	TSet<FName> Sections;
	for (int32 Index = 0; Index < Segments.Num(); ++Index)
	{
		const FAgentMcpMontageSegment& Segment = Segments[Index];
		UAnimSequence* Animation = LoadAsset<UAnimSequence>(Segment.Animation);
		if (!Animation)
		{
			Problems.Add(FString::Printf(TEXT("segments[%d]: '%s' is not an animation sequence"), Index, *Segment.Animation));
		}
		else if (!UsesSkeleton(Animation, Skeleton))
		{
			Problems.Add(FString::Printf(TEXT("segments[%d]: %s uses another skeleton"), Index, *Segment.Animation));
		}
		bool bRepeated = false;
		Sections.Add(FName(*Segment.Section), &bRepeated);
		if (Segment.Section.TrimStartAndEnd().IsEmpty() || bRepeated)
		{
			Problems.Add(FString::Printf(TEXT("segments[%d]: section '%s' is empty or taken"), Index, *Segment.Section));
		}
		if (!(Segment.PlayRate > 0.0))
		{
			Problems.Add(FString::Printf(TEXT("segments[%d]: playRate %g is not positive"), Index, Segment.PlayRate));
		}
		if (Segment.LoopCount < 1)
		{
			Problems.Add(FString::Printf(TEXT("segments[%d]: loopCount %d is below 1"), Index, Segment.LoopCount));
		}
		Animations.Add(Animation);
	}
	for (int32 Index = 0; Index < Segments.Num(); ++Index)
	{
		if (!Segments[Index].NextSection.IsEmpty() && !Sections.Contains(FName(*Segments[Index].NextSection)))
		{
			Problems.Add(FString::Printf(TEXT("segments[%d]: nextSection '%s' is not a section of the montage"), Index, *Segments[Index].NextSection));
		}
	}
	if (ReportProblems(Problems))
	{
		return Result;
	}

	if (!Montage)
	{
		UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
		Factory->TargetSkeleton = Skeleton;
		Factory->SourceAnimation = Animations[0];
		Montage = Cast<UAnimMontage>(CreateAsset(PackageName, AssetName, UAnimMontage::StaticClass(), Factory));
		if (!Montage)
		{
			RaiseToolError(TEXT("ASSET_CREATE_FAILED"), FString::Printf(TEXT("The montage %s could not be created."), *PackageName), TEXT("log_get_recent may show the reason."));
			return Result;
		}
		Result.bCreated = true;
	}

	Montage->Modify();
	Montage->SlotAnimTracks.SetNum(1);
	FSlotAnimationTrack& Track = Montage->SlotAnimTracks[0];
	Track.SlotName = FName(*SlotName);
	Track.AnimTrack.AnimSegments.Reset();
	TArray<float> Starts;
	float Position = 0.f;
	for (int32 Index = 0; Index < Segments.Num(); ++Index)
	{
		FAnimSegment Segment;
		Segment.SetAnimReference(Animations[Index], /*bInitialize=*/true);
		Segment.AnimPlayRate = float(Segments[Index].PlayRate);
		Segment.LoopingCount = Segments[Index].LoopCount;
		Segment.StartPos = Position;
		Starts.Add(Position);
		Position += Segment.GetLength();
		Track.AnimTrack.AnimSegments.Add(Segment);
	}
	Montage->CompositeSections.Reset();
	for (int32 Index = 0; Index < Segments.Num(); ++Index)
	{
		Montage->AddAnimCompositeSection(FName(*Segments[Index].Section), Starts[Index]);
	}
	// A section without a next section ends the montage, so by default each one leads on to the one that follows it.
	for (int32 Index = 0; Index < Segments.Num(); ++Index)
	{
		const int32 SectionIndex = Montage->GetSectionIndex(FName(*Segments[Index].Section));
		if (!Montage->CompositeSections.IsValidIndex(SectionIndex))
		{
			continue;
		}
		const FString& Next = Segments[Index].NextSection;
		Montage->CompositeSections[SectionIndex].NextSectionName = !Next.IsEmpty() ? FName(*Next)
			: Segments.IsValidIndex(Index + 1) ? FName(*Segments[Index + 1].Section) : NAME_None;
	}
	Montage->SetCompositeLength(Position);
	Montage->BlendIn.SetBlendTime(float(BlendInSeconds));
	Montage->BlendOut.SetBlendTime(float(BlendOutSeconds));
	Montage->UpdateLinkableElements();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();

	Result.Asset = Montage->GetPathName();
	for (const FCompositeSection& Section : Montage->CompositeSections)
	{
		FAgentMcpMontageSection& Reported = Result.Sections.AddDefaulted_GetRef();
		Reported.Name = Section.SectionName.ToString();
		Reported.NextSection = Section.NextSectionName.IsNone() ? FString() : Section.NextSectionName.ToString();
		Reported.StartSeconds = Section.GetTime();
	}
	Result.PlayLength = Montage->GetPlayLength();
	Result.bDirty = Montage->GetPackage()->IsDirty();
	return Result;
}

FAgentMcpAnimBlueprintResult UAgentMcpAnimationTools::BuildAnimBlueprint(const FString& AssetPath, USkeleton* Skeleton, const FJsonObjectWrapper& Graph,
	const TArray<FAgentMcpAnimVariable>& Variables, TSubclassOf<UAnimInstance> ParentClass, bool bReplace)
{
	using namespace UE::AgentMcp;
	using namespace UE::AgentMcp::AnimationToolsPrivate;

	FAgentMcpAnimBlueprintResult Result;
	if (!Tools::RequireObject(Skeleton, TEXT("skeleton")))
	{
		return Result;
	}
	FString PackageName;
	FString AssetName;
	UAnimBlueprint* AnimBlueprint = nullptr;
	if (!PrepareTarget(TEXT("anim_build_anim_blueprint"), AssetPath, bReplace, PackageName, AssetName, AnimBlueprint))
	{
		return Result;
	}

	TArray<FString> Problems;
	UClass* Parent = AnimBlueprint ? AnimBlueprint->ParentClass.Get() : (ParentClass.Get() ? ParentClass.Get() : UAnimInstance::StaticClass());
	if (!AnimBlueprint && (!Parent->IsChildOf(UAnimInstance::StaticClass()) || !FKismetEditorUtilities::CanCreateBlueprintOfClass(Parent)))
	{
		Problems.Add(FString::Printf(TEXT("an Animation Blueprint cannot derive from %s"), *Parent->GetName()));
	}
	if (AnimBlueprint && ParentClass.Get() && ParentClass.Get() != Parent)
	{
		Problems.Add(FString::Printf(TEXT("the Animation Blueprint at %s derives from %s and keeps its parent"), *PackageName, *GetNameSafe(Parent)));
	}
	if (AnimBlueprint && AnimBlueprint->TargetSkeleton != Skeleton)
	{
		Problems.Add(FString::Printf(TEXT("the Animation Blueprint at %s targets the skeleton %s"), *PackageName, *GetPathNameSafe(AnimBlueprint->TargetSkeleton)));
	}
	TMap<FName, FString> RequestedVariables;
	for (int32 Index = 0; Index < Variables.Num(); ++Index)
	{
		const FAgentMcpAnimVariable& Variable = Variables[Index];
		if (Variable.Name.IsEmpty() || (Variable.Type != TEXT("Float") && Variable.Type != TEXT("Bool")))
		{
			Problems.Add(FString::Printf(TEXT("variables[%d]: needs a name and the type Float or Bool"), Index));
			continue;
		}
		RequestedVariables.Add(FName(*Variable.Name), Variable.Type);
	}
	const TSharedPtr<FNodePlan> Plan = PlanNode(Graph.JsonObject, TEXT("graph"), Skeleton, Problems);
	TArray<TPair<FName, FString>> Uses;
	CollectVariables(*Plan, Uses);
	for (const TPair<FName, FString>& Use : Uses)
	{
		const FProperty* Property = FindFProperty<FProperty>(Parent, Use.Key);
		const bool bBlueprintVariable = AnimBlueprint && FBlueprintEditorUtils::FindNewVariableIndex(AnimBlueprint, Use.Key) != INDEX_NONE;
		const FString* Requested = RequestedVariables.Find(Use.Key);
		if (Property ? !IsNumericProperty(Property) : (!bBlueprintVariable && (!Requested || *Requested != TEXT("Float"))))
		{
			Problems.Add(FString::Printf(TEXT("%s reads '%s', which is not a number variable of the parent class or the Blueprint; add it with variables"),
				*Use.Value, *Use.Key.ToString()));
		}
	}
	if (ReportProblems(Problems))
	{
		return Result;
	}

	if (!AnimBlueprint)
	{
		UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
		Factory->TargetSkeleton = Skeleton;
		Factory->ParentClass = Parent;
		Factory->BlueprintType = BPTYPE_Normal;
		AnimBlueprint = Cast<UAnimBlueprint>(CreateAsset(PackageName, AssetName, UAnimBlueprint::StaticClass(), Factory));
		if (!AnimBlueprint)
		{
			RaiseToolError(TEXT("ASSET_CREATE_FAILED"), FString::Printf(TEXT("The Animation Blueprint %s could not be created."), *PackageName), TEXT("log_get_recent may show the reason."));
			return Result;
		}
		Result.bCreated = true;
	}
	AnimBlueprint->Modify();

	for (const TPair<FName, FString>& Variable : RequestedVariables)
	{
		if (FindFProperty<FProperty>(Parent, Variable.Key) || FBlueprintEditorUtils::FindNewVariableIndex(AnimBlueprint, Variable.Key) != INDEX_NONE)
		{
			continue;
		}
		FEdGraphPinType PinType;
		if (Variable.Value == TEXT("Float"))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		}
		else
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		}
		if (FBlueprintEditorUtils::AddMemberVariable(AnimBlueprint, Variable.Key, PinType))
		{
			Result.AddedVariables.Add(Variable.Key.ToString());
		}
	}

	UEdGraph* AnimGraph = nullptr;
	for (UEdGraph* FunctionGraph : AnimBlueprint->FunctionGraphs)
	{
		if (FunctionGraph && FunctionGraph->GetFName() == UEdGraphSchema_K2::GN_AnimGraph)
		{
			AnimGraph = FunctionGraph;
			break;
		}
	}
	UAnimGraphNode_Root* Root = nullptr;
	TArray<UEdGraphNode*> OldNodes;
	for (UEdGraphNode* Node : AnimGraph ? AnimGraph->Nodes : TArray<TObjectPtr<UEdGraphNode>>())
	{
		if (UAnimGraphNode_Root* RootNode = Cast<UAnimGraphNode_Root>(Node))
		{
			Root = RootNode;
		}
		else if (Node)
		{
			OldNodes.Add(Node);
		}
	}
	UEdGraphPin* ResultPin = Root ? Root->FindPin(TEXT("Result")) : nullptr;
	if (!AnimGraph || !ResultPin)
	{
		RaiseToolError(TEXT("NOT_SUPPORTED"), FString::Printf(TEXT("%s has no anim graph with an output pose."), *AnimBlueprint->GetPathName()));
		return Result;
	}
	AnimGraph->Modify();
	for (UEdGraphNode* Node : OldNodes)
	{
		FBlueprintEditorUtils::RemoveNode(AnimBlueprint, Node, /*bDontRecompile=*/true);
	}
	ResultPin->BreakAllPinLinks();

	const UEdGraphSchema* Schema = AnimGraph->GetSchema();
	int32 Row = 0;
	auto BindInput = [&](UEdGraphNode* Node, const TCHAR* PinName, const FNodeInput& Input)
	{
		UEdGraphPin* Pin = Node->FindPin(PinName);
		if (!Pin || !Input.bSet)
		{
			return;
		}
		if (Input.Variable.IsNone())
		{
			Schema->TrySetDefaultValue(*Pin, FString::SanitizeFloat(Input.Constant));
			return;
		}
		FGraphNodeCreator<UK2Node_VariableGet> Creator(*AnimGraph);
		UK2Node_VariableGet* Getter = Creator.CreateNode();
		Getter->VariableReference.SetSelfMember(Input.Variable);
		Getter->NodePosX = Node->NodePosX - 240;
		Getter->NodePosY = Node->NodePosY + 120;
		Creator.Finalize();
		if (UEdGraphPin* Value = Getter->FindPin(Input.Variable))
		{
			Schema->TryCreateConnection(Value, Pin);
		}
	};
	TFunction<UEdGraphPin*(const FNodePlan&, int32)> Build = [&](const FNodePlan& Node, int32 Depth) -> UEdGraphPin*
	{
		const int32 PosX = Root->NodePosX - 360 * (Depth + 1);
		const int32 PosY = Root->NodePosY + 200 * Row;
		if (Node.Type == TEXT("Slot"))
		{
			FGraphNodeCreator<UAnimGraphNode_Slot> Creator(*AnimGraph);
			UAnimGraphNode_Slot* Slot = Creator.CreateNode();
			Slot->Node.SlotName = Node.Slot;
			Slot->NodePosX = PosX;
			Slot->NodePosY = PosY;
			Creator.Finalize();
			if (UEdGraphPin* SourcePose = Node.Source.IsValid() ? Build(*Node.Source, Depth + 1) : nullptr)
			{
				Schema->TryCreateConnection(SourcePose, Slot->FindPin(TEXT("Source")));
			}
			return Slot->FindPin(TEXT("Pose"));
		}
		if (Node.Type == TEXT("Inertialization"))
		{
			FGraphNodeCreator<UAnimGraphNode_Inertialization> Creator(*AnimGraph);
			UAnimGraphNode_Inertialization* Inertialization = Creator.CreateNode();
			Inertialization->NodePosX = PosX;
			Inertialization->NodePosY = PosY;
			Creator.Finalize();
			if (UEdGraphPin* SourcePose = Node.Source.IsValid() ? Build(*Node.Source, Depth + 1) : nullptr)
			{
				Schema->TryCreateConnection(SourcePose, Inertialization->FindPin(TEXT("Source")));
			}
			return Inertialization->FindPin(TEXT("Pose"));
		}
		if (Node.Type == TEXT("BlendSpacePlayer"))
		{
			FGraphNodeCreator<UAnimGraphNode_BlendSpacePlayer> Creator(*AnimGraph);
			UAnimGraphNode_BlendSpacePlayer* Player = Creator.CreateNode();
			Player->SetAnimationAsset(Node.BlendSpace);
			Player->NodePosX = PosX;
			Player->NodePosY = PosY;
			Creator.Finalize();
			BindInput(Player, TEXT("X"), Node.X);
			BindInput(Player, TEXT("Y"), Node.Y);
			++Row;
			return Player->FindPin(TEXT("Pose"));
		}
		FGraphNodeCreator<UAnimGraphNode_SequencePlayer> Creator(*AnimGraph);
		UAnimGraphNode_SequencePlayer* Player = Creator.CreateNode();
		Player->SetAnimationAsset(Node.Sequence);
		Player->Node.SetLoopAnimation(Node.bLoop);
		Player->NodePosX = PosX;
		Player->NodePosY = PosY;
		Creator.Finalize();
		++Row;
		return Player->FindPin(TEXT("Pose"));
	};
	if (UEdGraphPin* Pose = Build(*Plan, 0))
	{
		Schema->TryCreateConnection(Pose, ResultPin);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	Result.Compile = UAgentMcpBlueprintTools::Compile(AnimBlueprint);
	Result.AnimBlueprint = AnimBlueprint->GetPathName();
	Result.ParentClass = GetPathNameSafe(AnimBlueprint->ParentClass);
	Result.Skeleton = GetPathNameSafe(AnimBlueprint->TargetSkeleton);
	for (const UEdGraphNode* Node : AnimGraph->Nodes)
	{
		if (Node)
		{
			// The source string, so the titles read the same whatever language the editor runs in.
			Result.Nodes.Add(Node->GetNodeTitle(ENodeTitleType::ListView).BuildSourceString());
		}
	}
	return Result;
}
