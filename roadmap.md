# Luma Roadmap

Last updated: March 12, 2026

## Scope of this audit

This roadmap is based on a repo scan of the current working tree, not only the last clean commit.

Inputs reviewed:

- `README.md`
- `TODO.md`
- `CMakeLists.txt`
- key docs under `docs/`
- engine/editor entry points and major subsystem files
- current git working tree changes
- existing automated test coverage

Validation performed:

- `ctest -C Debug --output-on-failure` in `build-vs18`
- Result: `Luma.Smoke.OpenGL`, `Luma.Smoke.LuaRuntime`, `Luma.Smoke.AudioRuntime`, `Luma.Smoke.SceneRuntime`, and `Luma.Smoke.PrefabRuntime` passed on March 12, 2026

Important note:

- The current working tree already contains substantial in-progress work for Lua scripting, audio, prefab support, inspector panels, and content browser behavior.
- This roadmap treats those changes as "active implementation already underway", but not yet fully hardened until they are regression-tested and documented.

## Executive summary

Luma already has a strong editor-first alpha foundation. The project is no longer at the "blank engine" stage. Core boot flow, project handling, scene editing, import pipeline, resource streaming infrastructure, PhysX integration, package management, and a large amount of editor UI are real and running.

What is still missing is not basic scaffolding. The biggest remaining work is moving several systems from "feature foundation" to "reliable, polished, team-usable alpha":

1. Rendering still has visible feature gaps.
2. Materials are still driven by ad hoc component/json logic instead of a clean asset/runtime architecture.
3. The editor still lacks core production workflow features like undo/redo and live content watching.
4. QA automation is far behind implementation breadth.
5. Too much ownership still lives inside `TriangleLayer`, which will slow every future feature.

The repo is closest to an "alpha foundation with several advanced vertical slices" rather than a "feature-complete engine alpha".

## Current state snapshot

| Area | Status | What exists now | What is still missing or risky |
| --- | --- | --- | --- |
| Application boot and startup UX | Green | Project browser, startup transition, splash-driven boot flow, smoke-test launch path | Needs more regression coverage across real project open/reload paths |
| Build and runtime target | Yellow | OpenGL alpha target is clear and the smoke test passes | Still OpenGL-only; backend abstraction exists but other backends are not implemented |
| Scene ECS and serialization | Green | Scene/entity/component flow is real, broad component serialization exists, play/stop restore flow exists, and headless round-trip serialization smoke coverage now exists | Needs broader compatibility coverage over time, but the baseline save/load path is now regression-tested |
| Prefab workflow | Yellow | Create, serialize, instantiate, apply, revert, status, override inspection, override-level revert, dedicated prefab docs, and headless lifecycle smoke coverage are already present | Needs hardening, nested/variant strategy, and broader regression depth |
| Content browser and thumbnails | Yellow | Async folder refresh, cached folder tree, progressive thumbnails, drag/drop import and instantiate flows, plus polling-based content-root change detection for external file edits | Large project behavior still depends on refresh-based invalidation in some paths; this is not OS-native file watching yet |
| Asset import pipeline | Yellow | Registry, sidecars, deterministic import hashes, reimport, built-in importers, drag/drop integration, CLI | Editor-generated import settings UI is still missing; richer asset model work remains |
| Cooker and platform output | Yellow | Cook command scaffold exists | Platform-specific compression/optimization and validation are still missing |
| Resource streaming framework | Yellow | Async service, budgets, events, texture promotion, mesh manifest/chunk streaming, live editor visibility | No GPU-native mesh residency yet; audio/package consumers are declared at API level but not finished as real runtime paths |
| Rendering pipeline | Yellow | RHI abstractions, OpenGL backend, lighting data flow, streamed scene meshes, render item assembly, sky preview tooling, and unsupported bloom controls are now hidden from the post-process inspector until the pass is real | Sky rendering in the active backend path is incomplete; bloom is still stored but not rendered; feature truth is ahead of docs in some places and behind in others |
| Material system | Red | Material inspector, shared `.lumamat` files, material proxy cache, per-item scene rendering, imported texture assignment | Still component/json driven; planned material asset/runtime split is not actually established as dedicated engine subsystems |
| Physics core | Yellow | PhysX backend active, rigid bodies, colliders, joints, controllers, event dispatch, scene serialization | Alpha stability, deterministic tests, heavy-scene tuning, and regression coverage remain open |
| Vehicles | Yellow | Vehicle input separation and baseline runtime simulation exist | Tuning workflow, tire behavior, assists, and polish are still incomplete |
| Destruction | Yellow | Blast foundation is integrated and runtime fracture flow exists | Authoring pipeline, chunk controls, collision quality, cleanup/perf, and gameplay hooks remain major work |
| Lua scripting | Yellow | Script engine, metadata parsing, property overrides, hot reload, runtime instance lifecycle, physics callbacks, large inspector UI, dedicated lifecycle/hot-reload smoke coverage, and scripting docs with example scripts | Still needs broader regression depth, more docs over time, and continued stabilization as the branch grows |
| Audio | Yellow | Miniaudio runtime, clip caching, 2D/3D playback, listener/source components, inspector preview, play mode update path, and docs for listener/source/editor behavior | Still needs broader tests and a final decision on whether streaming audio becomes a real alpha consumer or stays deferred |
| Package manager | Yellow | Registry refresh, search, install/update/remove/verify, lock/manifest handling, mount roots, editor tool launching on Windows | Needs broader QA, clearer production package format guarantees, and better non-Windows expectations |
| Plugin system | Red | Plugin config plumbing exists and the empty plugin panel surface is now hidden until built-in plugins are actually registered | Built-in plugin registry is still empty, so the feature remains structurally present but functionally not populated |
| Editor workflow maturity | Red | Strong panel set, project settings, content workflows, prefab actions, inspector depth, and polling-based content-root refresh for external edits | No real undo/redo transaction system found; this is still a major day-to-day usability gap |
| Test coverage and CI | Yellow | OpenGL smoke plus Lua runtime lifecycle/hot-reload smoke, audio runtime smoke, scene serialization round-trip smoke, and prefab lifecycle smoke now exist in `ctest` | The matrix is still far from complete, especially for physics, packages, asset import, and visual rendering regression |
| Architectural maintainability | Red | Good service abstractions exist in places | Core orchestration is still too concentrated in very large files like `src/Layers/TriangleLayer.cpp`, `LuaScriptRuntime.cpp`, `PackageManager.cpp`, and `PhysXBackend.cpp` |

## Key findings

### 1. The largest delivery gap is feature completion, not project setup

Luma already boots, opens projects, imports assets, edits scenes, renders, simulates physics, and runs editor workflows. The next stage is finishing the hard edge cases and removing "stored but not actually rendered/consumed" features.

Examples:

- Sky authoring exists, but the active backend path still does not fully render the authored sky model.
- Bloom values are exposed and blended, but the fullscreen bloom pass is still pending.
- Streaming supports texture and mesh consumers, but audio is still mostly independent from the streaming layer.

### 2. Materials are the biggest architectural debt area

The current material path works, but it is not yet a clean engine subsystem.

Right now:

- `.lumamat` is effectively loaded as a json-backed `MaterialComponent`
- proxy generation happens through `TriangleLayer` helpers
- the dedicated material asset/resource/shader-map architecture described in `docs/unreal_material_system_plan.md` is only partially realized in practice

This is manageable short-term, but it will become a blocker for:

- masked/translucent correctness
- shader permutations
- material inheritance
- renderer portability
- long-term editor graph work

### 3. Editor production workflows are still behind the feature set

The editor is feature-rich, but still missing some "daily driver" necessities:

- undo/redo transaction history
- filesystem watching for content roots
- stricter workflow-level regression tests
- documentation that matches the actual current implementation

Without these, a growing team will feel friction even when core systems technically work.

### 4. Docs are now drifting behind implementation

Several docs still describe prefab support as future work, but prefab creation/instancing/apply/revert already exists in the current tree.

That means the team now needs a documentation sync pass, not just more implementation.

### 5. The current codebase has several monolithic ownership hotspots

Notable high-risk files by size:

- `src/Layers/TriangleLayer.cpp`: 4879 lines
- `engine/runtime/Scripting/LuaScriptRuntime.cpp`: 3646 lines
- `engine/runtime/Asset/Package/PackageManager.cpp`: 3038 lines
- `engine/runtime/Physics/Backends/PhysXBackend.cpp`: 2648 lines

This does not mean the code is bad, but it does mean future work will keep getting slower unless more responsibilities are extracted into focused services.

## What is left to fully implement

### P0: Alpha truth, reliability, and workflow baseline

These are the highest-value tasks because they unblock every other team member.

- Add subsystem regression tests beyond the one OpenGL smoke test
- [x] Add scene save/load regression coverage (headless round-trip smoke)
- [x] Add prefab create/apply/revert regression coverage
- Add Lua runtime smoke tests for lifecycle, hot reload, and property overrides
- Add physics regression scenes for rigid bodies, triggers, vehicles, and destruction
- Sync docs to current implementation so the roadmap and code tell the same story
- [x] Add filesystem watching for content roots (polling-based watcher in editor loop)
- Implement an editor undo/redo transaction system

### P1: Rendering and material completion

These are the biggest user-visible alpha gaps.

- Wire sky rendering into the active render backend path
- Either implement bloom or remove/hide unsupported bloom controls until it is real
- Audit every post-process field and mark each one as shipped, partial, or deferred
- Preserve material slot metadata through cooked mesh workflows
- Replace ad hoc material json/component loading with dedicated material asset/runtime services
- Establish a real material compile identity and runtime resource path before adding more shader features
- Improve masked/transparency correctness and imported material fidelity

### P1: Asset and content workflow completion

- Build the editor import settings UI from importer schemas
- Improve import feedback, validation, and reimport visibility in the editor
- Decide final alpha scope for `.lumamesh` authoring vs runtime-only behavior
- Add platform-aware cook validation and output verification
- Tighten content browser behavior for large projects and mounted package roots

### P2: Physics completion

The physics stack is already advanced, but still clearly on an alpha track.

- Heavy-scene stability tuning
- Deterministic core-body regression coverage
- Vehicle tuning asset workflow completion
- Tire slip/grip model polish
- Destruction authoring pipeline from source mesh to runtime asset
- Chunk collision quality and cleanup/pooling for large fracture events

### P2: Scripting, audio, and gameplay support completion

- Turn current Lua implementation into a tested and documented supported feature
- [x] Formalize script authoring docs and examples
- Decide whether audio streaming becomes a first-class runtime path or remains independent for alpha
- Add audio regression tests for preview, play mode listener handoff, 2D/3D playback, and imported `.lumaaudio`

### P2: Package, plugin, and extensibility maturity

- Harden package registry/source validation
- Add more package QA around install/update/rollback/mounting
- Decide whether plugin UI should ship before there are actual built-in plugins
- Populate the built-in plugin registry or hide/defer the surface until real plugins exist

### P3: Architecture cleanup

This should run in parallel with features, not only after them.

- Extract material asset IO and proxy building out of `TriangleLayer`
- Extract prefab diff/revert logic into its own service
- Break Lua runtime bindings/metadata/runtime lifecycle into smaller units
- Continue moving editor behaviors into host/service layers
- Reduce subsystem coupling inside `TriangleLayer`

## Recommended delivery phases

### Phase 0: Stabilize the baseline

Goal:

- make current implementation trustworthy

Deliverables:

- documentation sync pass
- regression test plan
- first real subsystem smoke suite
- decision on alpha-supported vs alpha-deferred features

Exit criteria:

- every major subsystem has at least one automated smoke or golden-path test
- roadmap and docs agree on current truth
- unsupported editor controls are either implemented or explicitly marked deferred

### Phase 1: Close the visual alpha gap

Goal:

- make the renderer feel intentionally complete for alpha use

Deliverables:

- active sky rendering path
- bloom implementation or scope removal
- post-process truth audit
- material workflow cleanup

Exit criteria:

- the renderer matches the editor controls for all alpha-supported lighting/post-process features
- imported assets retain predictable material behavior across raw and cooked workflows

### Phase 2: Finish production editor workflows

Goal:

- make the editor safe for daily team use

Deliverables:

- undo/redo
- content root watching
- improved import UX
- prefab regression coverage

Exit criteria:

- artists/designers can edit scenes without high fear of destructive mistakes
- external content changes appear reliably
- prefab workflows survive repeated apply/revert cycles

### Phase 3: Physics and gameplay hardening

Goal:

- move physics, scripting, and audio from "works" to "dependable"

Deliverables:

- physics stress tuning
- vehicle/destruction completion
- script lifecycle tests
- audio reliability tests

Exit criteria:

- no major play/stop regressions
- no high-severity prefab/script/physics state corruption issues
- target gameplay test scenes stay stable

### Phase 4: Architecture and extensibility cleanup

Goal:

- reduce future feature cost

Deliverables:

- service extraction from `TriangleLayer`
- material subsystem separation
- plugin/package surface cleanup
- CI expansion

Exit criteria:

- new features no longer require deep edits across large orchestration files
- subsystem boundaries are clear enough for multiple developers to work in parallel safely

## Recommended Trello board structure

Suggested lists:

1. `Backlog`
2. `Ready`
3. `In Progress`
4. `Blocked`
5. `Review / QA`
6. `Done`

Suggested labels:

- `Core`
- `Rendering`
- `Editor`
- `Assets`
- `Physics`
- `Scripting`
- `Audio`
- `Packages`
- `Plugins`
- `QA`
- `Docs`
- `Tech Debt`
- `Alpha Blocker`

Suggested card template:

- Summary
- Why this matters
- Scope
- Dependencies
- Acceptance criteria
- Test plan
- Notes / links

## Trello-ready epic cards

Use these as the first board population.

### Epic 1: Alpha truth and regression baseline

- Audit all editor-visible features and mark each as shipped, partial, or deferred
- Expand automated tests beyond `Luma.Smoke.OpenGL`
- [x] Add scene save/load regression coverage
- [x] Add prefab lifecycle regression coverage
- Add scripting lifecycle smoke tests
- Add package manager smoke coverage

### Epic 2: Rendering parity and visual completion

- Wire sky rendering into the active backend render path
- Implement bloom pass or remove unsupported bloom controls
- Audit and lock the alpha-supported post-process feature set
- Validate imported opaque/cutout/fade/transparent behavior
- Improve masked/transparency behavior in the runtime render path

### Epic 3: Material system cleanup

- Extract `.lumamat` loading/saving into a dedicated material asset service
- Define real material asset vs runtime resource boundaries
- Add material compile identity and cache strategy
- Preserve material slot metadata in cooked mesh workflows
- Stop relying on `TriangleLayer` helper logic for core material ownership

### Epic 4: Editor production workflow maturity

- Implement undo/redo transaction history
- Add filesystem watching for content roots
- Harden content browser refresh/invalidation behavior
- Improve import progress and reimport visibility
- Add "feature unavailable" UX for deferred editor controls

### Epic 5: Prefab workflow hardening

- [x] Add prefab regression coverage for create/apply/revert
- Add prefab override-level revert regression coverage
- Harden prefab diffing and source-missing handling
- Decide support plan for nested prefabs
- Decide support plan for prefab variants
- [x] Sync prefab docs to actual implementation

### Epic 6: Physics alpha completion

- Tune heavy-scene physics stability
- Add rigid body and collider regression scenes
- Complete vehicle tuning asset workflow
- Improve vehicle tire and differential behavior
- Build destruction authoring pipeline
- Improve chunk collision quality and fracture cleanup

### Epic 7: Lua and gameplay scripting maturity

- Formalize supported Lua authoring model
- Add script metadata and override tests
- [x] Add hot reload regression tests
- Add physics callback regression tests
- [x] Write scripting documentation and sample scripts

### Epic 8: Audio completion

- [x] Add audio regression coverage for 2D/3D playback
- Validate `.lumaaudio` import and runtime resolution
- Decide alpha scope for streamed audio
- [x] Document listener/source behavior in editor and play mode

### Epic 9: Package and plugin maturity

- Harden registry refresh, install, update, verify, and rollback flows
- Add package mount regression scenarios
- Decide whether plugin UI ships before actual built-in plugins
- Populate the built-in plugin registry or hide the empty surface

### Epic 10: Architecture debt reduction

- Extract material logic from `TriangleLayer`
- Extract prefab diff/apply/revert logic into services
- Break down `LuaScriptRuntime.cpp`
- Break down `PhysXBackend.cpp`
- Continue host/service extraction from monolithic editor orchestration

## Suggested first 15 cards to create immediately

If the team wants a short actionable starting set, create these first:

1. `Define alpha-supported feature matrix`
2. `[x] Expand ctest suite beyond OpenGL smoke`
3. `[x] Add scene serialization regression coverage`
4. `Implement editor undo/redo transaction system`
5. `[x] Add filesystem watching for content roots`
6. `Wire sky rendering into active backend path`
7. `[x] Implement bloom pass or remove unsupported bloom UI`
8. `Extract material asset IO out of TriangleLayer`
9. `Preserve material slot metadata in cooked mesh assets`
10. `[x] Add prefab lifecycle regression coverage`
11. `Complete vehicle tuning asset workflow`
12. `Build destruction authoring pipeline`
13. `[x] Add Lua lifecycle and hot reload smoke tests`
14. `Decide alpha scope for audio streaming`
15. `[x] Populate built-in plugin registry or hide plugin panel`

## Definition of done for alpha

Luma should be considered "alpha-ready" when all of the following are true:

- Opening a project, editing a scene, saving, reloading, and entering/exiting play mode are reliable
- Major editor workflows have undo/redo or another safe recovery mechanism
- Sky, lighting, and supported post-process features match the editor controls that are exposed
- Material handling is predictable across imported assets and shared material assets
- Physics is stable in target scenes, including vehicles and supported destruction workflows
- Lua scripting and audio are documented and regression-tested if they remain enabled features
- Package and content workflows behave predictably for teams
- Documentation matches the actual implementation
- CI catches the most common regressions before developers do

## What should be deferred until after alpha

These should not distract the team unless they become required by a near-term product goal:

- additional renderer backends beyond OpenGL
- full node-based material graph authoring
- deep plugin marketplace ecosystem work
- broad package ecosystem expansion before package format and QA are stable
- nonessential visual bells and whistles before core render parity is complete

## Final recommendation

The best next move is not "add more features everywhere". The best next move is:

1. lock feature truth
2. close the biggest alpha-facing gaps in rendering and editor workflow
3. add test coverage around the features that already exist
4. reduce the `TriangleLayer` and material-system ownership bottlenecks before they slow the team further

If the team follows that order, Luma can move from "impressive in-progress engine" to "usable alpha platform" much faster than by starting another major subsystem from scratch.
