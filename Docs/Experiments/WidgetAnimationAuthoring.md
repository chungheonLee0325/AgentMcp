# Experiment: authoring widget animations with a tool

Status: planned. Run it in a session of its own, and keep its code out of the plugin until the result is known.

## Question

Can an editor tool add a UMG widget animation with a property track and keys to a Widget Blueprint, without the Widget Blueprint
editor, so that the Widget Blueprint compiles and the animation plays in Play In Editor?

Unreal Engine 5.8 does not answer this: its UMGToolSet reads animation bindings but has no function that creates or edits
animations. In Unreal Engine 5.5 the Widget Blueprint editor creates the animation object in code, but tracks and keys are added
through Sequencer.

The agreed policy does not depend on the answer: simple motion stays in C++ (see the `umg-authoring` skill), and timeline animations
are made in the Widget Blueprint editor. A tool would only let agents make or change timeline animations as well.

## Done when

1. A tool call adds the animation `Fade` (1 second) to a fixture Widget Blueprint.
2. The animation has one track: `RenderOpacity` of the widget `Panel`, with the keys 0 at 0 s and 1 at 1 s.
3. `blueprint_compile` reports no errors, and `umg_inspect` lists the animation and its binding to `Panel`.
4. In Play In Editor, a C++ base with `BindWidgetAnim` plays the animation on construct, and two captures show different opacity.
5. `editor_undo` removes the animation again.

Optional, only after 1-5: a render transform track, and saving and reopening the asset.

## Budget

- Stop after the first full attempt at step 4, or when three different ways of adding the track have failed. Then write the
  findings, also when the answer is no.
- Search engine code and read only the functions that matter (grep with a few lines of context). Do not read whole engine files.
- Take at most four captures; check structure with the text of `umg_inspect`.
- Put the experiment code in the testbed editor module (`Source/AgentMcpTestbedEditor`) as a toolset named `experiment`. The plugin
  DLLs are locked while another editor loads the plugin; the testbed modules are not.
- Use a new fixture Widget Blueprint and C++ base; do not change `Content/Samples`.

## Starting points (Unreal Engine 5.5)

Checked:

- `Engine/Source/Editor/UMGEditor/Private/TabFactory/AnimationTabSummoner.cpp`: the "+ Animation" button creates the animation with
  `NewObject<UWidgetAnimation>(WidgetBlueprint, FName(), RF_Transactional)`.
- `Engine/Source/Runtime/UMG/Public/Animation/WidgetAnimation.h` and `Private/Animation/WidgetAnimation.cpp`:
  `UWidgetAnimation::BindPossessableObject`, `CanPossessObject` and `LocateBoundObjects`.
- `Engine/Source/Runtime/UMG/Public/Animation/`: `WidgetAnimationBinding.h`, `MovieScene2DTransformTrack.h`,
  `MovieSceneMarginTrack.h` and `MovieSceneWidgetMaterialTrack.h`.
- `Engine/Source/Editor/UMGEditor/Private/Animation/UMGDetailKeyframeHandler.cpp`: whether a property can be keyed is asked from
  Sequencer (`CanKeyProperty`).
- `Engine/Source/Editor/UMGEditor/Private/WidgetBlueprintCompiler.cpp`: `BindWidgetAnim` properties must be `Transient`.

Search terms that have not been checked: `UMovieScene::AddPossessable`, `UMovieScene::AddTrack`, `UMovieSceneFloatTrack`,
`SetPropertyNameAndPath`, `FMovieSceneFloatChannel`, `TickResolution`, `DisplayRate`.

## Findings

Write here what worked, the engine calls in order, what failed and why, and whether a tool is worth building.
