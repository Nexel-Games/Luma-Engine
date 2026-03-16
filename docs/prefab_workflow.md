# Prefab Workflow

## Overview

Luma supports prefab asset authoring and prefab instance workflows in the editor.

Current prefab capabilities:

- create a `.lumaprefab` from a selected entity hierarchy
- instantiate a prefab asset from the `Content Browser`
- mark instantiated entities with prefab source metadata
- apply an instance hierarchy back to the prefab asset
- revert an instance hierarchy from the prefab asset
- inspect prefab status and per-component override groups
- revert overrides at the component or field level

## Creating A Prefab

You can create a prefab from the current scene hierarchy in two common ways:

1. Select the entity you want as the prefab root.
2. Use `Create Prefab` from the hierarchy context menu.

Luma writes a `.lumaprefab` asset and marks the existing hierarchy as a prefab instance rooted at that asset.

## Instantiating A Prefab

To place a prefab back into the scene:

1. Open the `Content Browser`.
2. Find the `.lumaprefab` asset.
3. Double-click it or use its instantiate action.

Result:

- a new scene hierarchy is created
- the hierarchy is tagged with `PrefabInstanceComponent`
- the instance root and children keep source entity ids for diff and revert workflows

## Apply And Revert

### Apply Prefab

`Apply Prefab` writes the current instance hierarchy back into the prefab asset on disk.

Use it when your scene edits should become the new prefab source.

### Revert Prefab

`Revert Prefab` replaces the instance hierarchy with a fresh instantiate from the source prefab asset.

Use it when local scene edits should be discarded.

## Override Inspection

The inspector shows a `Prefab Instance` section for entities that belong to a prefab instance.

That section can show:

- prefab asset label
- instance root label when you are inspecting a child
- prefab status such as `Up to Date` or `Modified`
- grouped override sections by component
- field-level override rows

Supported revert actions:

- `Revert Component`
- per-field `Revert`

## Current Notes

- Prefab lifecycle smoke coverage now exists for create, instantiate, apply, and revert paths.
- Audio source and audio listener components are now carried through prefab cloning.
- Nested prefabs and prefab variants still need a clearer support plan before they should be treated as alpha-stable.

## Recommended Workflow

1. Build or import the hierarchy you want in the scene.
2. Create a prefab from the root entity.
3. Instantiate the prefab wherever you need reuse.
4. Use the inspector to review override groups.
5. Apply only when you want to update the source asset.
6. Revert when local edits should be discarded.
