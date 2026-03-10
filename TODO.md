# Luma TODO

This file is the active roadmap for the root `C:\Luma` engine codebase.
It supersedes planning notes under `LumaEngine/` when working on the current root `src/` + `include/` target.

## Scope and assumptions

- The active build is the root target in `CMakeLists.txt`.
- `engine/` and `LumaEngine/` are treated as legacy or parallel code unless explicitly revived.
- Priorities are ordered by what most improves correctness, buildability, and forward momentum.

## Priority legend

- P0: Must fix to make the active codebase trustworthy.
- P1: Core runtime/editor functionality needed for a coherent engine.
- P2: Important feature completion and architecture cleanup.
- P3: Polish, scale, and release work.

## P0 - Establish one active engine

- [ ] Declare the root `src/` + `include/` tree as the single source of truth.
  - Done when: a root architecture note says which directories are active, experimental, deprecated, or archival.
- [ ] Audit `engine/`, `LumaEngine/`, and duplicate docs for overlap with the root code.
  - Done when: every duplicate subsystem is either archived, deleted, or linked back to the active implementation.
- [ ] Replace stale docs that reference the wrong engine layout.
  - Done when: docs no longer describe `engine/runtime/...` as the active path unless that tree is intentionally revived.
- [ ] Add a root `README.md` that explains how to build, run, and navigate the active codebase.
  - Done when: a new contributor can build and launch the editor without reading source first.

## P0 - Make the active branch buildable from clean

- [ ] Build the root target from a clean checkout and record every compile, link, and runtime-startup failure.
  - Done when: a clean local build succeeds in Debug and Release.
- [ ] Fix or remove incomplete code that is currently referenced but not implemented.
  - Known suspect area: package registry helpers declared in `TriangleLayer`.
  - Done when: there are no unresolved symbols or dead declarations in the active target.
- [ ] Add a documented supported toolchain matrix.
  - Done when: compiler, generator, SDK, and dependency expectations are explicit.
- [ ] Verify shader copy/post-build steps for both OpenGL and Vulkan builds.
  - Done when: first launch from a fresh build finds required shaders without manual copying.

## P0 - Remove fake or misleading behavior

- [ ] Remove boot-flow messages that imply systems exist when they are only placeholders.
  - Examples: script compile, reflection rebuild, plugin load, module load.
  - Done when: startup progress reflects real work or clearly marks simulated phases.
- [ ] Either wire project VSync settings through the render backends or stop exposing the setting as functional.
  - Done when: toggling VSync changes runtime behavior on supported backends.
- [ ] Either finish package registry UI wiring or hide/disable it until it works.
  - Done when: the UI is either functional end-to-end or intentionally absent.
- [ ] Rename or document the current PhysX backend honestly.
  - Done when: users understand that the current backend is a fallback solver unless native PhysX is actually linked.

## P1 - Scene persistence and project loading

- [ ] Implement scene serialization and deserialization for the active `Scene` system.
  - Done when: the editor can save and load `.scene` or `.lumascene` files reliably.
- [ ] Wire `Project::GetConfig().startScene` into startup.
  - Done when: opening a project loads the configured start scene instead of always seeding an in-memory default scene.
- [ ] Define a stable scene file format and versioning strategy.
  - Done when: scene schema upgrades are possible without silent breakage.
- [ ] Serialize all currently authored core components.
  - Minimum set: transform, hierarchy, camera, mesh renderer, material, light, rigid body, collider, sky light.
  - Done when: round-tripping a scene preserves entity state.
- [ ] Add dirty-state tracking for scenes and prompt on unsaved changes.
  - Done when: editor close/open flows protect user work.
- [ ] Add scene creation, save-as, duplicate, and recent-scene actions.
  - Done when: scene management is part of normal editor workflow.

## P1 - Turn the renderer into a real scene renderer

- [ ] Stop treating the scene as one rebuilt combined primitive mesh.
  - Done when: rendering consumes a list of scene draw items instead of a single flattened mesh.
- [ ] Introduce a renderable asset reference path for `MeshRendererComponent`.
  - Done when: entities can point to imported meshes, not only procedural primitives.
- [ ] Define a render proxy extraction layer from ECS to renderer.
  - Done when: scene data is transformed into stable per-frame render commands with clear ownership.
- [ ] Separate editor-only visuals from scene geometry.
  - Examples: grid, selection outlines, collider debug, gizmos, sky preview.
  - Done when: editor overlays are rendered as overlays, not baked into scene mesh data.
- [ ] Replace the one built-in triangle shader path with a minimal but real material/render path.
  - Done when: multiple materials, textures, and mesh assets can render in the viewport.
- [ ] Add proper camera selection rules.
  - Done when: viewport can use editor camera or selected scene camera predictably, including FOV/near/far.
- [ ] Add resize-safe scene render targets for both backends.
  - Done when: viewport resize does not leak resources or corrupt rendering.

## P1 - Finish backend correctness

- [ ] Verify OpenGL backend resource lifetime, descriptor emulation, and viewport output behavior.
  - Done when: repeated resize, asset reload, and editor reopen cycles stay stable.
- [ ] Verify Vulkan backend swapchain, render pass, descriptor, and ImGui integration under resize and minimized-window conditions.
  - Done when: Vulkan survives resize, alt-tab, and lost-surface paths without crashes.
- [ ] Make backend selection errors user-facing and actionable.
  - Done when: unsupported backend choices explain what failed and how to recover.
- [ ] Add backend capability reporting.
  - Done when: editor can display active API, shader path, limits, and disabled features.

## P1 - Make material and texture data real

- [ ] Decide which fields in `MaterialComponent` are runtime-authoritative vs editor-only metadata.
  - Done when: the component shape matches actual renderer consumption.
- [ ] Load texture assets referenced by `MaterialComponent` and bind them in the active pipeline.
  - Done when: albedo/normal/ORM/emissive paths affect viewport output.
- [ ] Replace hard-coded default textures with asset-aware fallback rules.
  - Done when: missing assets fall back cleanly and visibly.
- [ ] Add per-material shader variant handling only where justified.
  - Done when: variant explosion is controlled and documented.
- [ ] Add a dedicated sky rendering path.
  - Done when: sky is not brightness-compensated through the lit object material path.

## P1 - Bring physics surface area in line with implementation

- [ ] Decide whether to ship native PhysX soon or keep the fallback solver as the active backend.
  - Done when: CMake, runtime labels, docs, and expectations match reality.
- [ ] If keeping fallback solver, limit exposed editor features to what it actually supports.
  - Done when: joints, vehicles, ragdolls, buoyancy, and force fields are hidden or marked unsupported until implemented.
- [ ] If shipping native PhysX, complete linking, initialization, actor creation, scene stepping, and transform sync.
  - Done when: native backend runs real PhysX simulation.
- [ ] Add collision layers, queries, and debug visualization with consistent semantics.
  - Done when: users can inspect collisions without guessing current solver behavior.
- [ ] Make collider mesh usage deterministic.
  - Done when: mesh collider bounds or cooked collider data are cached and versioned properly.

## P1 - Asset pipeline integration

- [ ] Connect imported mesh assets to `MeshRendererComponent` and viewport rendering.
  - Done when: importing a model creates something the scene can render directly.
- [ ] Connect imported texture assets to materials and thumbnails.
  - Done when: importing textures lets users assign and preview them in-editor.
- [ ] Connect imported sky assets to `SkyLightComponent`.
  - Done when: HDRI imports can be assigned without manual file path guessing.
- [ ] Add asset-to-scene drag/drop behavior.
  - Done when: dragging a mesh, material, or sky asset into the viewport or hierarchy creates the expected entity updates.
- [ ] Define asset GUID/reference semantics.
  - Done when: scene references survive file moves and reimports.
- [ ] Add asset dependency tracking to support safe delete, rename, and move operations.
  - Done when: editor can warn about broken references before changes are committed.

## P1 - Project browser and templates

- [ ] Finish project templates beyond labels and thumbnails.
  - Done when: First Person and Third Person templates generate meaningful project content instead of placeholder defaults.
- [ ] Create at least one canonical sample project inside the active repo or as a documented external sample.
  - Done when: there is a known-good project that exercises rendering, assets, and scene loading.
- [ ] Validate recent-project handling and failure recovery.
  - Done when: moved or deleted projects do not poison the browser UX.
- [ ] Add per-project engine compatibility checks.
  - Done when: opening old or incompatible projects surfaces clear upgrade warnings.

## P2 - Break up `TriangleLayer`

- [ ] Split `TriangleLayer.cpp` into focused editor panels and services.
  - Suggested targets: Hierarchy panel, Inspector panel, Viewport panel, Console panel, Content browser, Project settings, Preferences, Package UI.
  - Done when: no single editor source file owns the entire product surface.
- [ ] Introduce a small editor state/service layer instead of direct field sprawl.
  - Done when: selection, viewport state, import tasks, and project settings drafts have clear owners.
- [ ] Move math helpers and scene-mesh generation out of the editor layer.
  - Done when: rendering prep code is testable outside ImGui panel code.
- [ ] Separate editor command registration from panel rendering.
  - Done when: console/command palette commands are easy to test and extend.

## P2 - Formalize the render pipeline layer

- [ ] Define what `CoreLite` and `CoreX` actually mean in terms of features, performance targets, and backend requirements.
  - Done when: choosing a pipeline has documented consequences.
- [ ] Move from one hard-coded demo-style pipeline to a real extensible scene pipeline contract.
  - Done when: pipeline code consumes scene render data, lights, materials, and render settings without one-off hacks.
- [ ] Expand the render graph beyond setup + one main pass.
  - Done when: sky, depth/prepass, opaque, transparency, post, and editor overlays can be modeled cleanly.
- [ ] Add shader hot reload if it is intended as a supported workflow.
  - Done when: editing shaders triggers deterministic reload and recovery on compile failure.

## P2 - Editor UX and workflow

- [ ] Add undo/redo for scene edits.
  - Done when: transforms, entity creation/deletion, parenting, and component edits are reversible.
- [ ] Add copy/paste and duplicate for entities and components.
  - Done when: common scene authoring operations do not require manual recreation.
- [ ] Add multi-select aware inspector actions.
  - Done when: transform and shared-property editing works across selections.
- [ ] Add scene gizmo snapping, local/world controls, and selection outline polish.
  - Done when: viewport manipulation feels consistent and intentional.
- [ ] Improve content browser filtering, sorting, and context actions.
  - Done when: users can rename, move, delete, reimport, and reveal assets from one place.
- [ ] Add dock layout save/restore.
  - Done when: editor layout persists between sessions.

## P2 - Input framework completion

- [ ] Decide whether the current action/context system is the permanent input API.
  - Done when: naming, supported devices, and rebinding semantics are locked down.
- [ ] Add broader keyboard and mouse coverage, then gamepad support if planned.
  - Done when: input mapping is not blocked by the current tiny enum surface.
- [ ] Persist editor and project input bindings.
  - Done when: rebinding survives restart.
- [ ] Add input debugging UI.
  - Done when: raw inputs, action values, active contexts, and claimed actions are inspectable.

## P2 - Package system completion

- [ ] Decide whether packages are a core feature for the current milestone or a deferred one.
  - Done when: scope is explicit.
- [ ] If core, finish registry refresh, manifest fetch, install, mount, verify, and hot-load workflows in the active editor.
  - Done when: users can discover and install packages from the editor without broken states.
- [ ] If deferred, narrow the system to local archive install only and remove exposed unfinished registry flows.
  - Done when: the product only advertises what works.
- [ ] Add package security and provenance checks.
  - Done when: hash validation and failure reporting are enforced.

## P2 - Runtime architecture cleanup

- [ ] Introduce a clear runtime/editor boundary.
  - Done when: editor-only systems are not mixed into runtime scene or renderer code without intent.
- [ ] Decide whether `Application` remains a single-window editor shell or evolves toward a game/runtime launcher architecture.
  - Done when: the boot model is documented and coherent.
- [ ] Add a proper scene-play mode transition if gameplay simulation is intended.
  - Done when: editor state and runtime state can be separated safely.

## P2 - Diagnostics and developer ergonomics

- [ ] Add structured logging categories and severity controls that persist in editor settings.
  - Done when: logs are filterable and useful in both console and file output.
- [ ] Add crash-safe logging and startup diagnostics.
  - Done when: startup failures can be debugged from logs alone.
- [ ] Add GPU and CPU frame diagnostics to the viewport.
  - Done when: users can inspect frame cost, draw counts, resource counts, and backend state.
- [ ] Add validation toggles for Vulkan and asset pipeline diagnostics.
  - Done when: debug builds provide actionable validation output.

## P3 - Tests and CI

- [ ] Add a first-party test target for core systems.
  - Minimum set: project config, scene graph, asset registry, import hashing, render graph ordering, math helpers.
  - Done when: core logic is covered by automated tests.
- [ ] Add smoke tests for editor startup and backend selection.
  - Done when: CI can catch regressions that stop the app from launching.
- [ ] Add golden-image or deterministic scene-render tests once rendering is stable enough.
  - Done when: major rendering regressions are caught automatically.
- [ ] Add CI for Debug and Release builds.
  - Done when: pull requests cannot merge without passing builds.

## P3 - Performance and scalability

- [ ] Profile viewport rendering on representative scenes.
  - Done when: there is a baseline for frame time, allocations, and resource churn.
- [ ] Avoid rebuilding full scene mesh data every frame for unchanged content.
  - Done when: scene extraction is incremental or cached.
- [ ] Reduce runtime allocations in editor frame loops.
  - Done when: common frame paths stop constructing large temporary vectors and strings unnecessarily.
- [ ] Add resource streaming and cache invalidation strategy for larger content sets.
  - Done when: the asset system does not assume tiny projects.

## P3 - Documentation and onboarding

- [ ] Write a root architecture document for the active engine.
  - Done when: runtime, editor, renderer, physics, asset pipeline, and package system responsibilities are mapped clearly.
- [ ] Document project file format, scene format, and asset formats.
  - Done when: format evolution is deliberate.
- [ ] Add contributor docs for coding standards, module boundaries, and review expectations.
  - Done when: contributors know where new code belongs.
- [ ] Add a milestone roadmap that reflects the active codebase, not the legacy branch.
  - Done when: planning docs no longer send work into dead directories.

## Suggested execution order

1. Lock down which engine tree is active.
2. Make the active root target build clean from scratch.
3. Remove or disable misleading/incomplete surfaced features.
4. Implement scene save/load and wire project start scene.
5. Convert rendering from combined primitive mesh output to real scene draw data.
6. Connect asset imports to scene rendering.
7. Decide native PhysX vs fallback solver and align surface area.
8. Split `TriangleLayer` into maintainable editor modules.
9. Finish package system scope.
10. Add tests, CI, profiling, and docs.

## Release criteria for a coherent milestone

- [ ] Clean build from a fresh checkout.
- [ ] Project browser can create and open a project successfully.
- [ ] Project loads its configured start scene.
- [ ] Scene can be saved, closed, reopened, and round-tripped without data loss.
- [ ] Viewport renders imported meshes and material textures, not only primitives.
- [ ] OpenGL and Vulkan both run through the same real scene-render path.
- [ ] Physics labeling and behavior match actual implementation.
- [ ] Asset import, reimport, and assignment flows work in-editor.
- [ ] No obviously exposed placeholder UI remains.
- [ ] Core systems have first-party automated coverage.
