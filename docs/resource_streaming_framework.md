# Resource Streaming Framework API

## Overview

Luma now exposes a dedicated resource streaming API for editor and runtime-facing systems that need:

- async loading
- background streaming
- memory budget control
- LOD-aware streaming requests

The implementation is file-backed and CPU-resident at the service layer, with active consumers now wired into the runtime texture path, cooked mesh asset imports, streamed scene mesh decode, external mesh chunk residency, and primitive LOD selection. It gives the engine a stable API surface now, and leaves room for later GPU-native mesh residency and more advanced distance-driven policies.

## Public Headers

- `include/Luma/Asset/Streaming/ResourceStreamingTypes.h`
- `include/Luma/Asset/Streaming/IResourceStreamProvider.h`
- `include/Luma/Asset/Streaming/IResourceStreamingService.h`
- `include/Luma/Asset/Streaming/FileResourceStreamProvider.h`
- `include/Luma/Asset/Streaming/ResourceStreamingService.h`

## Core Concepts

### `StreamRequestDesc`

Defines what to stream.

Important fields:

- `key`: stable caller-facing identifier used for deduplication and release-by-key
- `sourcePath`: absolute or project-relative source file
- `resourceType`: texture, mesh, audio, buffer, package blob, procedural
- `priority`: background -> critical
- `lod`: target LOD request and fallback rules
- `persistent`: opt out of automatic eviction
- `evictable`: allow budget eviction
- `estimatedCpuBytes` / `estimatedGpuBytes`: residency accounting hints

### `StreamPayload`

Represents the loaded result.

Current implementation stores:

- resolved source path
- raw file bytes
- CPU/GPU residency estimates
- resolved LOD
- fallback flag
- content tag

### `StreamRecord`

Tracks request lifecycle and residency state:

- `Queued`
- `Streaming`
- `Resident`
- `Evicted`
- `Failed`
- `Cancelled`

## Service Lifecycle

Create and initialize once per project/editor session:

```cpp
Luma::Assets::ResourceStreamingService streaming;
std::string error;
if (!streaming.Initialize(projectRoot, error))
{
    // handle init failure
}

streaming.RegisterProvider(std::make_shared<Luma::Assets::FileResourceStreamProvider>());
streaming.SetBudget(Luma::Assets::StreamingBudget {});
```

Tick every frame:

```cpp
streaming.Tick();
```

Shutdown cleanly:

```cpp
streaming.SetEventCallback({});
streaming.Shutdown();
```

## Async Loading

Requests are queued immediately and executed on the shared engine job system.

```cpp
Luma::Assets::StreamRequestDesc request {};
request.key = "Textures/Sky/Clouds";
request.sourcePath = "Assets/Textures/clouds.ktx2";
request.resourceType = Luma::Assets::StreamResourceType::Texture;
request.priority = Luma::Assets::StreamPriority::High;

std::string error;
const auto handle = streaming.Request(request, error);
```

The request returns a `StreamRequestHandle` immediately. Background workers load the payload asynchronously.

## Background Streaming

Background work is driven by `JobSystem::Dispatch()`.

The service:

- keeps a queued request list
- dispatches up to `budget.maxInFlightRequests`
- collects completed worker results on the main thread during `Tick()`
- emits events for queued, started, completed, failed, evicted, released, and retargeted states

## Memory Budget Control

Budgeting is explicit:

```cpp
Luma::Assets::StreamingBudget budget {};
budget.maxCpuResidentBytes = 512ull * 1024ull * 1024ull;
budget.maxGpuResidentBytes = 1024ull * 1024ull * 1024ull;
budget.maxInFlightRequests = 4;
budget.maxResidentRecords = 256;
budget.enableEviction = true;

streaming.SetBudget(budget);
```

Eviction policy currently prefers:

1. lower priority resources first
2. older untouched resident resources next
3. non-persistent and evictable records only

`persistent == true` prevents automatic eviction.

## LOD Streaming

LOD requests are part of the request description:

```cpp
request.lod.mode = Luma::Assets::LODStreamingMode::Explicit;
request.lod.targetLod = 2;
request.lod.minLod = 0;
request.lod.maxLod = 4;
request.lod.allowLowerDetailFallback = true;
```

The default file provider probes LOD file variants using these patterns:

- `Name_LOD2.ext`
- `Name_lod2.ext`
- `Name.LOD2.ext`
- `Name.lod2.ext`
- `LOD2/Name.ext`
- `lod2/Name.ext`

If fallback is enabled, lower-detail candidates are attempted before falling back to the base file.

Retarget an existing stream request:

```cpp
std::string error;
streaming.RetargetLOD(handle, 1, error);
```

If a resident resource is retargeted, the current residency is dropped and the request is re-queued.

## Querying State

Single-record query:

```cpp
Luma::Assets::StreamRecord record {};
if (streaming.TryGetRecord(handle, record))
{
    // inspect record.state, record.resolvedLod, record.lastError
}
```

Payload query:

```cpp
Luma::Assets::StreamPayload payload {};
if (streaming.TryGetPayload(handle, payload))
{
    // consume payload.bytes
}
```

Bulk inspection:

```cpp
const auto records = streaming.GetRecords();
const auto stats = streaming.GetStats();
```

## Releasing and Cancelling

Cancel without deleting the record:

```cpp
streaming.Cancel(handle);
```

Release completely:

```cpp
streaming.Release(handle);
streaming.ReleaseByKey("Textures/Sky/Clouds");
```

## Event Callback

The service exposes a lightweight event channel:

```cpp
streaming.SetEventCallback(
    [](const Luma::Assets::StreamEvent& event)
    {
        // route to logs, editor notifications, or task UI
    });
```

Use it for:

- editor console lines
- content browser feedback
- streaming profiler overlays
- task progress integration

`TriangleLayer` now also routes stream lifecycle into a non-blocking editor task record so active queue and in-flight work can surface in the engine task system without modal-blocking the editor.

## Current Editor Integration

`TriangleLayer` now:

- initializes the streaming service with the current project root
- registers `FileResourceStreamProvider`
- ticks the service every frame
- shuts it down on detach
- shows live streaming state inside `Window > GPU Resources`
- exposes console control for request/cancel/release/list/budget/LOD
- mirrors failures and evictions into the editor console
- mirrors active queue and in-flight work into the editor task system

## Texture Consumer Path

`TextureSystem` now has an actual streaming consumer path.

### What happens

1. `CreateExternalTextureReference()` routes to `CreateStreamedTextureReference()` when a streaming service is bound.
2. A stable GPU texture handle is created immediately as a 1x1 placeholder.
3. A background texture stream request is queued.
4. `TextureSystem::TickStreaming()` polls for completed payloads.
5. Decodable image payloads are uploaded into the existing texture handle through the backend `UpdateTexture()` path.

### Supported promotion formats

The current promotion path supports:

- `.png`
- `.jpg`
- `.jpeg`
- `.bmp`
- `.tga`
- `.hdr`
- `.exr`
- `.lumatex`
- `.lumasky`

`TextureSystem` now reuses the same intermediate-header and HDR/EXR decode model already used by the editor thumbnail service, so streamed texture references can promote from common engine intermediate texture payloads instead of staying on placeholder pixels.

## Mesh Consumer Path

The mesh path now has two consumers on top of the streaming API.

### Cooked mesh import path

Model imports (`.obj`, `.fbx`, `.gltf`, `.glb`) now cook into `.lumamesh` payloads backed by the shared mesh codec in:

- `include/Luma/Asset/Core/MeshAssetIO.h`
- `src/Asset/Core/MeshAssetIO.cpp`

Cooked mesh imports now produce:

- a `.lumamesh` manifest
- one `.lmshchunk` file per cooked mesh section

The manifest stores:

- versioned mesh header
- source name
- mesh bounds
- section bounds
- per-section relative chunk paths

Each `.lmshchunk` stores:

- packed `PrimitiveVertex` data
- packed index data

Legacy `.lumamesh` intermediate payloads are still supported through compatibility decode.

### Runtime scene mesh path

`TriangleLayer` now consumes non-primitive `MeshRendererComponent` entries through the streaming service:

1. resolves the mesh source path
2. queues a mesh manifest stream request
3. waits for the `.lumamesh` manifest to become resident
4. decodes the manifest through `LoadMeshAssetManifestFromBytes()`
5. determines active sections from distance and LOD policy
6. queues only the active `.lmshchunk` section payloads
7. decodes those section payloads through `LoadMeshChunkPayloadFromBytes()`
8. composes the resident section geometry into the combined scene mesh

This means mesh renderers can now render streamed `.lumamesh`, `.obj`, `.fbx`, `.gltf`, and `.glb` content instead of being limited to built-in primitives.

### Automatic mesh LOD selection

`MeshRendererComponent` now exposes:

- `Auto Stream LOD`
- `Max Auto LOD`
- `LOD Near Distance`
- `LOD Far Distance`

When automatic LOD is enabled, the editor computes a requested mesh LOD from camera distance to the entity and requests the matching streamed mesh variant automatically.

When automatic LOD is disabled, the explicit `Mesh LOD` field is used.

### Section-based external mesh residency

Cooked mesh payloads are now written as a manifest plus external section chunk files instead of one flat cooked blob.

Each section stores:

- local section bounds
- section vertex data
- section index data

At runtime, `TriangleLayer` can compose only the active sections for a mesh renderer using:

- `Distance Sections`
- `Section Load Distance`

This is the first pass of real large-environment mesh streaming. The runtime scene path no longer has to decode every section up front. It streams the lightweight manifest first, then requests only the active external section chunks needed for the current camera distance.

### Mesh asset picker

The `Mesh Renderer` inspector no longer requires a raw typed path for streamed meshes.

When `Use Primitive` is disabled:

- `Select Mesh Asset...` opens a mesh picker popup
- the popup searches across content roots
- supported assets are filtered to mesh formats only
- the selected asset is stored back into `meshSource`

This makes streamed mesh assignment work as an editor workflow instead of a path-entry-only debug workflow.

### Primitive LOD path

Primitive meshes still expose explicit LOD variants through `PrimitiveMeshFactory::GetPrimitive(type, lod)`.

Current primitive LOD-aware types:

- sphere
- cylinder
- capsule
- cone
- torus

`TriangleLayer` consumes this through `streaming.meshlod`, which changes the primitive tessellation level used to build the combined scene mesh.

Physics mesh half-extents and thumbnail mesh previews now use the same shared mesh loader as the importer and scene path, so cooked `.lumamesh` and legacy `.lumamesh` both resolve consistently across runtime and editor systems.

## Console Commands

The editor console now exposes:

- `streaming.request <path> [type] [priority] [lod]`
- `streaming.cancel <handle|key>`
- `streaming.release <handle|key>`
- `streaming.lod <handle> <lod>`
- `streaming.budget [cpuMB gpuMB inFlight resident]`
- `streaming.list`
- `streaming.meshlod <0-3>`

## What This Version Does Not Yet Do

This API is intentionally infrastructure-first. It does **not** yet:

- stream imported mesh buffers directly into dedicated GPU mesh buffers outside the combined scene mesh rebuild path
- switch mesh/material LODs by distance
- integrate with platform cook data

The remaining mesh gap is deliberate: mesh assets are now cooked and streamable as external chunk payloads, but they are still decoded into CPU-side `PrimitiveMeshData` before being folded into the renderable scene mesh. The next step is GPU-ready mesh residency, not another ad-hoc importer path.

## Practical Next Steps

1. Add GPU-native mesh chunk uploads so streamed sections do not require scene-mesh recomposition.
2. Add material/section LOD policies on top of the same manifest+chunk format.
3. Add streaming-aware editor notifications on top of the event/task hooks.
