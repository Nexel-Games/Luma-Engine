# Mesh Drag And Drop

## Overview

Luma now supports dragging mesh assets from the Content Browser into:

- the Viewport
- the Hierarchy
- a specific entity row in the Hierarchy

Supported source asset types:

- `.obj`
- `.fbx`
- `.gltf`
- `.glb`
- `.lumamesh`

This creates a scene entity configured as a mesh instance using `MeshRendererComponent` and `MaterialComponent`.

## How To Use

### Drag Into The Viewport

1. Open `Content Browser`.
2. Find a mesh asset such as `Sponza.obj` or `Sponza.fbx`.
3. Click and drag the asset tile.
4. Drop it into the `Viewport`.

Result:

- A new entity is created.
- The entity is spawned in front of the editor camera.
- `Use Primitive` is disabled automatically.
- The dropped mesh path is assigned to `Mesh Source`.
- If a cooked `.lumamesh` with the same file stem already exists beside the dropped source, Luma uses that cooked mesh automatically.
- A default material component is created.
- The new entity becomes selected.

### Drag Into The Hierarchy

1. Drag a mesh asset from the `Content Browser`.
2. Drop it into empty space in the `Hierarchy` panel.

Result:

- A new root-level scene entity is created.
- The entity starts at the scene origin.
- The mesh asset is assigned automatically.

### Drag Onto An Existing Entity

1. Drag a mesh asset from the `Content Browser`.
2. Drop it directly onto an entity row in the `Hierarchy`.

Result:

- A new entity is created as a child of the target entity.
- The child uses the dropped mesh asset.
- The child starts with local position `(0, 0, 0)` relative to the parent.

## What Gets Created

Each dropped mesh creates:

- `TransformComponent`
- `MeshRendererComponent`
- `MaterialComponent`

Mesh renderer defaults:

- `Visible = true`
- `Use Primitive = false`
- `Mesh Source = <dropped asset>`
- `Auto Stream LOD = true`
- `Mesh LOD = 0`

Material defaults:

- `Material Name = MI_<EntityName>`
- default white tint

## Mesh Source Path Rules

Luma stores the dropped mesh as:

- project-relative path when the asset is inside the loaded project
- absolute path only when it is outside the project root

When a raw source mesh such as `.obj` or `.fbx` has a cooked sibling like `sponza.lumamesh`, Luma stores the cooked mesh path instead.

This keeps project assets portable and compatible with the existing mesh resolution path.

## Notes

- This creates a scene instance immediately. It does not create a serialized prefab asset file yet.
- `.obj` and `.fbx` work directly because the mesh loading path still supports raw mesh decode.
- Cooked `.lumamesh` assets are preferred automatically when they already exist and will use the streaming mesh manifest + chunk path.
- Viewport drops are intended for quick scene placement.
- Hierarchy drops are intended for scene organization and parenting.

## Recommended Workflow For Sponza

1. Import or place `Sponza.obj` or `Sponza.fbx` in the project content.
2. Drag it from `Content Browser` into the `Viewport` for immediate placement.
3. If you want it under another scene node, drag the mesh onto that node in `Hierarchy` instead.
4. Adjust transform, material, and streaming settings from `Inspector` after creation.

## Current Limitation

This workflow creates a prefab-style scene entity instance, not a reusable prefab asset on disk. If you want true prefab asset authoring next, the next step is:

- add prefab asset serialization
- add `Create Prefab From Selection`
- add prefab instance/update workflows
