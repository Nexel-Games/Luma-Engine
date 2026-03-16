# Material And Texture Application Workflow

This document explains how imported mesh materials and textures are now applied in the editor, and what to use for the correct result.

## Summary

Luma now renders scene mesh entities as separate draw items instead of flattening every entity into one shared scene mesh.

That change is what allows:

- one dropped mesh to keep its own textures
- different mesh parts to use different materials
- OBJ / FBX / GLTF / GLB scene imports to create textured child entities

## What Works Now

Supported textured scene-source drops:

- `.obj`
- `.fbx`
- `.gltf`
- `.glb`

When you drag one of those raw source meshes into the `Viewport` or `Hierarchy`, Luma now:

1. loads the source scene with Assimp
2. extracts scene parts / material slots
3. creates a parent entity
4. creates one child entity per mesh part
5. assigns:
   - `Mesh Renderer`
   - `Material`
   - discovered texture paths

## Important Rule

If you want automatic material and texture application, drag the raw source mesh.

Use:

- `sponza.obj`
- `sponza.fbx`

Do not use:

- `sponza.lumamesh`

Reason:

The current `.lumamesh` runtime path preserves geometry streaming, but it does not yet preserve the original authoring scene graph and material-slot mapping in the same way as the raw scene import path.

So:

- raw source mesh = geometry + scene parts + material textures
- cooked `.lumamesh` = geometry-first runtime asset

## Sponza Workflow

For your Sponza project:

1. Open the project.
2. In `Content Browser`, go to:
   - `Sponza-master/Sponza-master`
3. Drag `sponza.obj` into the `Viewport`.
4. Luma will create:
   - one parent entity for the asset
   - multiple child entities for the Sponza submeshes
5. Each child gets its own `Material Component` with texture paths filled in.

If scene-part extraction fails, the editor now writes a warning into the console.

## Render Behavior

Mesh entities are now rendered through per-item scene draws.

That means each entity can bind:

- its own albedo texture
- its own normal texture
- its own ORM texture
- its own tint

Sky and grid still render through the shared scene mesh path.

## Inspector Expectations

For a correctly materialized raw import:

- the selected parent entity may only act as a container
- the child entities hold the visible submeshes
- each child should show:
  - `Use Primitive = false`
  - `Mesh Source = raw scene source`
  - texture fields populated in `Material`

## Current Limitations

These are still real limits:

1. Direct `.lumamesh` drops do not automatically rebuild multi-material scene-part structure.
2. Opacity / masked material behavior is not fully represented in the runtime shader path yet.
3. Emissive and advanced material features are still behind the current simplified scene shader.
4. This is still not a full prefab/material-import pipeline like Unreal, but imported hierarchies can now be turned into `.lumaprefab` assets after placement and then managed through the prefab workflow.

## If You See Geometry But No Textures

Check these first:

1. You dropped `sponza.obj`, not `sponza.lumamesh`.
2. The texture files still exist beside the source mesh / referenced material paths.
3. The child entities were created under the parent mesh entity.
4. The `Material Component` texture fields are populated.
5. The console does not show an `Import` warning for scene-part extraction.

## Recommended Usage Right Now

For architectural or marketplace assets:

1. drag the raw scene file into the scene
2. let Luma create materialized child entities
3. adjust transforms on the parent entity
4. keep the cooked `.lumamesh` path for runtime geometry streaming, not authoring-time material reconstruction

## Next Planned Improvements

The next logical upgrades are:

1. preserve material-slot metadata inside cooked mesh assets
2. auto-create material assets during import
3. support full masked / opacity texture rendering
4. improve prefab authoring ergonomics for imported scene hierarchies
