---
name: blueprint-graphs
description: Create Unreal Blueprint classes and read and edit their graphs (event graphs, functions, macros) through the Agent MCP blueprint tools - add variables, functions and event dispatchers, find node types, add and wire nodes, set pin values, compile and check the result in a play session. Use whenever a task calls blueprint_get_graph, blueprint_find_node_types, blueprint_edit_graph or blueprint_add_members, or asks to add or change Blueprint logic in an Unreal project that has the Agent MCP server.
---

# Blueprint graphs with Agent MCP

## Read before writing

- `blueprint_inspect` lists the graphs, variables, functions and components of a Blueprint.
- `blueprint_get_graph` lists the nodes of one graph with their pins, values and links. A link reads `Node.Pin`.
- In a large event graph, read one chain at a time: `bEntryPointsOnly` finds the events, and `connectedTo` with an event's node name
  returns only that chain.

## New classes and members

- A new Blueprint class: `asset_create` with `assetClass` `Blueprint` and a `parentClass` (`Actor`, `Character`, a C++ class of the
  project). Widget and Animation Blueprints have their own tools.
- `blueprint_add_members` adds variables (type, default value, category, Instance Editable, Expose on Spawn), functions with inputs
  and outputs, and event dispatchers in one call. Types are names: `Float`, `Integer`, `Vector`, `Actor` (an object reference),
  `Class of Actor`, `EMovementMode`, `Array of Name`, `Map of Name to Integer`.
- The result names each new function's graph and its entry and return nodes with their pins. Fill the function with
  `blueprint_edit_graph` on that graph: wire `<entry>.then` to the first node and the last node to `<return>.execute`, inputs come
  out of the entry node and outputs go into the return node.
- Component defaults of the class (mesh, mobility, collision) are set with `object_set_properties` on the `template` path that
  `blueprint_inspect` lists for each component, as in the Blueprint editor's details panel. Values that differ per placed actor are
  Instance Editable variables that the construction script applies.
- The getter, setter and call nodes of new members are node types at once. Search them by name (`GetHealth`, `SetHealth`, `Heal`):
  in a localized editor their categories are localized too, so do not write their type ids from memory.

## Write

1. **Look node types up; never guess them.** `blueprint_find_node_types` with a short filter (`PrintString`, `Branch`,
   `GetActorLocation`) returns type ids such as `Development|PrintString`, those whose name is or starts with the filter first. A
   filter that ends in `|` lists a category
   (`Utilities|FlowControl|`). `contextPin` keeps only node types that can connect to a pin you already have.
2. **Add the nodes.** `blueprint_edit_graph` with `Add` operations, each with a `ref` and a position. Lay the chain out from left to
   right, about 300 units per column. The result lists the new nodes with their exact pin names.
3. **Wire them.** `Connect` with `from` (an output, `ref.Pin`) and `to` (an input), and `SetDefault` for input values. Pin names come
   from step 2 or from `blueprint_get_graph`, not from memory: exec pins are `execute` and `then`, and a Branch has `Condition`,
   `then` and `else`.
   Steps 2 and 3 can be one call when the pin names are known.
4. **Compile once** with `blueprint_compile` after the graph edits of one logical change, and fix every error it reports. A node's
   own error also appears as `compilerMessage` in `blueprint_get_graph`.
5. **Check the behavior**, not just the compile: `pie_start`, then `log_get_recent` from the returned `startLogSequence` (Print
   String writes `[<Blueprint>_C_0] <text>` to the log), or `viewport_capture`. Then `pie_stop`, and `asset_save` when the change stays.

## Rules of the tool

- One call is one undo step (`editor_undo`). Node types, node names and refs are checked before anything changes; when a later
  operation fails, the whole call is undone and the error names the operation and the node's real pins.
- A type id that several classes declare needs `declaringClass`; the error lists them.
- `AddEvent|Custom|<Name>` adds a new custom event. Adding an event that the graph already has (`AddEvent|EventBeginPlay`) returns the
  existing node; the disabled grey event nodes of a new Blueprint are replaced.
- A custom event that a call adds can be called at once: its node type is `CallFunction|<Name>`.
- `AddPin` adds a pin to Sequence, Switch, Make Array, Select and math operator nodes.

## Blueprint habits

- Logic that returns values or is used from several places goes into a function graph; the event graph holds events and latent
  actions (Delay, timelines).
- A pure node runs again for every wire from its outputs. Store a result that several nodes use in a variable.
- A cast to a Blueprint class loads that Blueprint and everything it references, even when the cast fails. Prefer interfaces or C++
  base classes for loose coupling.
- Logic that is large, performance-sensitive or shared between many Blueprints belongs in C++; graphs call it.
