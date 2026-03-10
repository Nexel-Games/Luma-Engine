# Luma Material System Plan
## Purpose
This document translates the local `UnrealEngine` material architecture into a practical Luma implementation plan.

The goal is not to clone Unreal literally. The goal is to adopt the same separation of responsibilities that makes Unreal's material system scalable:
- authored material definition
- material instance inheritance and overrides
- compiled runtime resource
- render-thread-facing proxy
- shader translation and permutation control

## Unreal Source Areas Reviewed

Core ownership and asset/runtime split:
- `UnrealEngine/Engine/Source/Runtime/Engine/Classes/Materials/MaterialInterface.h`
- `UnrealEngine/Engine/Source/Runtime/Engine/Classes/Materials/Material.h`
- `UnrealEngine/Engine/Source/Runtime/Engine/Classes/Materials/MaterialInstance.h`
- `UnrealEngine/Engine/Source/Runtime/Engine/Private/Materials/Material.cpp`
- `UnrealEngine/Engine/Source/Runtime/Engine/Private/Materials/MaterialInstance.cpp`
- `UnrealEngine/Engine/Source/Runtime/Engine/Public/MaterialShared.h`
- `UnrealEngine/Engine/Source/Runtime/Engine/Private/Materials/MaterialShared.cpp`

Uniform expression and shader binding path:
- `UnrealEngine/Engine/Source/Runtime/Engine/Private/Materials/MaterialUniformExpressions.h`
- `UnrealEngine/Engine/Source/Runtime/Engine/Private/Materials/MaterialUniformExpressions.cpp`
- `UnrealEngine/Engine/Source/Runtime/Renderer/Public/MaterialShader.h`
- `UnrealEngine/Engine/Source/Runtime/Renderer/Public/MeshMaterialShader.h`
- `UnrealEngine/Engine/Source/Runtime/Engine/Public/MaterialCompiler.h`
- `UnrealEngine/Engine/Shaders/Private/MaterialTemplate.ush`

## Unreal Architecture Summary

### 1. Asset abstraction is separate from runtime resources
Unreal uses:
- `UMaterialInterface` as the shared material-facing abstraction
- `UMaterial` for the authored source definition
- `UMaterialInstance` for parent-linked overrides
- `FMaterial` and `FMaterialResource` for compiled runtime state
- `FMaterialRenderProxy` for render-time parameter binding

That is the most important design lesson for Luma. The material editor object, the gameplay-facing asset reference, the compiled shader state, and the per-draw parameter binding object must not be the same class.

### 2. Parameters are typed and named
Unreal does not model materials as a flat component full of ad-hoc fields. It uses typed parameter sets keyed by `FMaterialParameterInfo`:
- scalar
- vector
- texture
- static switch

This is what allows material instances to override values without redefining the entire material.

### 3. Instance overrides are cheap unless static parameters change
Normal parameter overrides are resolved into uniform expressions and bound through the render proxy.

Static switches and other compile-relevant settings affect shader-map identity and can trigger recompilation. This distinction matters because it keeps ordinary instance edits fast.

### 4. Shader generation is downstream of the asset model
Unreal's graph/compiler path eventually emits HLSL through the translator and `MaterialTemplate.ush`.

That is a later-stage concern. Before Luma can have a serious material graph, it needs a correct asset model and inheritance model.

## What Luma Should Copy

Luma should copy these architectural ideas:
- material asset interface layer
- base material vs instance asset split
- typed parameter overrides
- explicit compiled runtime material resource
- render proxy / resolved parameter binding layer
- compile identity that is separate from runtime instance values

Luma should not copy Unreal's complexity immediately:
- full editor graph compiler
- full shader-map DDC infrastructure
- all material domains and feature levels
- full parameter collection runtime

## Luma Material Architecture

### Layer 1: Asset-side source documents
Files:
- `include/Luma/Asset/Core/MaterialAsset.h`
- `include/Luma/Asset/Core/MaterialAssetIO.h`
- `engine/runtime/Asset/Core/MaterialAssetIO.cpp`

Responsibilities:
- define the `.lumamat` schema
- support graph assets and instance assets
- support typed parameter overrides
- resolve parent inheritance
- stay renderer-agnostic

This is the first rebuild step and the one implemented in this pass.

### Layer 2: Runtime material interface
Planned files:
- `include/Luma/Renderer/Material/MaterialInterface.h`
- `include/Luma/Renderer/Material/MaterialGraphResource.h`
- `include/Luma/Renderer/Material/MaterialInstanceResource.h`

Responsibilities:
- hold loaded material assets in memory
- expose a unified runtime interface to renderer and editor
- separate authored asset data from render-ready resolved data

This layer is the Luma equivalent of Unreal's `UMaterialInterface` plus the bridge to `FMaterialResource`.

### Layer 3: Compiled shader resource
Planned files:
- `include/Luma/RHI/MaterialShaderMap.h`
- `engine/runtime/RHI/MaterialShaderMap.cpp`
- `include/Luma/RHI/MaterialCompileKey.h`

Responsibilities:
- compute the compile-relevant identity of a material
- distinguish static parameters from dynamic overrides
- cache backend shader variants
- expose shader bindings required by the pipeline

This is the Luma equivalent of Unreal's shader-map and `FMaterialResource` tier.

### Layer 4: Render proxy
Planned files:
- `include/Luma/RHI/MaterialRenderProxy.h`
- `engine/runtime/RHI/MaterialRenderProxy.cpp`

Responsibilities:
- provide resolved scalar/vector/texture values at draw time
- bind material textures and uniform buffers
- isolate renderer submission from editor asset objects

This is the Luma equivalent of Unreal's `FMaterialRenderProxy`.

### Layer 5: Material graph compiler
Planned files:
- `include/Luma/MaterialGraph/*`
- `engine/runtime/MaterialGraph/*`
- shader templates under `assets/shaders/materials/*`

Responsibilities:
- translate graph nodes into backend shader code or shader-template defines
- emit compile-time parameter layout
- support static switches and domain/shading-model variants

This should come after the first four layers exist.

## Phase Plan

### Phase 1: Asset-side foundation
Status: implemented in this pass.

Deliver:
- `.lumamat` document model
- graph asset vs instance asset split
- typed parameter arrays
- inheritance resolution
- JSON read/write support

Success criteria:
- a material graph asset can be loaded from disk
- a material instance can resolve a parent and flatten overrides
- renderer/editor can consume the resolved result later without caring whether the source was graph or instance

### Phase 2: Runtime material registry
Status: foundation implemented in this pass.

Deliver:
- material asset cache
- loaded-material registry keyed by asset path or asset ID
- change detection / reload support

Success criteria:
- scene objects refer to material assets, not a giant scene component blob
- resolved materials are cached and reused

### Phase 3: Renderer integration
Deliver:
- material render proxy
- per-material uniform layout
- texture slot binding from resolved material assets
- blend/depth state from material properties

Success criteria:
- opaque, masked, translucent, additive, and modulate are driven from material assets
- OpenGL and future renderer backends consume the same resolved material description

### Phase 4: Static parameter and shader permutation split
Status: implemented in this pass.

Unreal reference used directly:
- `UnrealEngine/Engine/Source/Runtime/Engine/Private/Materials/MaterialShared.cpp`
  - `OutEnvironment.SetDefine(TEXT("USE_DITHERED_LOD_TRANSITION_FROM_MATERIAL"), ...)`
  - `OutEnvironment.SetDefine(TEXT("MATERIAL_TWOSIDED"), ...)`
  - `OutEnvironment.SetDefine(TEXT("MATERIAL_DOMAIN_*"), ...)`
  - `OutEnvironment.SetDefine(TEXT("MATERIAL_SHADINGMODEL_*"), ...)`
- `UnrealEngine/Engine/Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.h`
  - `OutEnvironment.SetDefine(TEXT("USES_WORLD_POSITION_OFFSET"), ...)`
- `UnrealEngine/Engine/Shaders/Private/BasePassVertexShader.usf`
  - `WorldPosition.xyz += GetMaterialWorldPositionOffset(VertexParameters);`
- `UnrealEngine/Engine/Shaders/Private/MaterialTemplate.ush`
  - `GetMaterialWorldPositionOffset(...)`

Deliver:
- explicit compile key
- static switch support separated from runtime parameter overrides
- shader cache per backend

Success criteria:
- ordinary instance edits do not rebuild shader code
- static switch edits rebuild only what is compile-relevant

Implemented Luma mapping:
- `MaterialCompileKey` now captures compile-relevant state only
- `MaterialShaderMap` resolves shader variants from that compile key
- render pipelines are cached per material compile identity
- shaders now consume Unreal-aligned material defines for two-sided, shading model, WPO, and dithered LOD feature toggles
- shadow/depth passes now use the same compile-key-driven feature toggles for masked opacity, two-sided culling, and WPO

### Phase 5: Authoring graph
Deliver:
- real graph asset authoring
- node translation layer
- material function support
- shared parameter collections

Success criteria:
- Luma materials are authored as reusable graph assets rather than only JSON surface descriptors

## Immediate Implementation Rules

### Rule 1
Do not reintroduce a scene-owned `MaterialComponent` as the core data model.

Scene entities should eventually reference material assets or material instance resources, not store the whole material authoring model inline.

### Rule 2
Do not couple material instances directly to backend shader objects.

The asset layer must remain renderer-agnostic.

### Rule 3
Do not treat every parameter as compile-relevant.

Static switches are compile-relevant. Scalar/vector/texture overrides are runtime data.

### Rule 4
Keep the resolved material description backend-agnostic.

Backend differences belong in the render proxy / shader-map layer, not in the asset schema.

## First Implemented Luma Mapping

The first Luma material foundation uses:
- `MaterialAssetDocument` as the asset source document
- `MaterialGraphAsset` as the authored base definition
- `MaterialInstanceAsset` as the parent-linked override asset
- `ResolvedMaterialAsset` as the flattened runtime-ready result
- `MaterialParameterInfo` plus typed parameter arrays to mirror Unreal's typed override model

This is intentionally narrower than Unreal, but it is the correct starting structure.

## Next Implementation Steps

1. Add a runtime material cache that loads and resolves `.lumamat` assets once.
2. Replace ad-hoc per-mesh surface data with material asset references.
3. Introduce a renderer-facing material render proxy.
4. Move blend/depth state selection fully under material asset control.
5. Split static switch compilation from dynamic parameter overrides.
6. Only after that, add a real graph authoring/compiler layer.
