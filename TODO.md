 ## TODO

 1. making camera markers toggleable from editor preferences so you can disable them entirely in heavy scenes.

 # Physics Status (Alpha Track)

_Last updated: March 11, 2026_

## Overview
This document tracks what is already implemented in Luma’s physics stack and what remains to ship a solid alpha.

## Implemented

### Core PhysX Integration
- PhysX backend is active and used as the main runtime physics path.
- Rigid body support is in place for static, dynamic, and kinematic actors.
- Common body properties are exposed and working (mass, gravity, damping, CCD, etc.).
- Collision simulation is wired into play mode and scene updates.

### Physics Components
- `RigidBodyComponent` is integrated with scene entities.
- Collider support exists for primary gameplay shapes (box/sphere/capsule and mesh-based colliders).
- Physics component serialization/deserialization is functional through scene save/load.

### Vehicle Framework (Foundation)
- Vehicle architecture moved to controller + wheel/data-driven model (not a monolithic component).
- Chassis uses standard rigid body path.
- Wheel/suspension/drivetrain data flow is in place.
- Vehicle input separation is established (input layer feeds simulation layer).
- Baseline runtime vehicle simulation loop is implemented and running.

### Blast / Destruction Foundation
- Destructible workflow has initial framework integration.
- Runtime fracture path exists and can spawn chunk actors.
- Play-mode destruction pipeline executes and can be re-triggered after restart (not one-shot-only anymore in foundation flow).

## Remaining Work

### Core Physics Polish
- Improve stability under heavy scenes (substepping policy, solver tuning, sleep thresholds).
- Tighten collision consistency (contact offsets, penetration recovery edge cases).
- Add deterministic regression tests for core body/collider interactions.
- Add stress/perf benchmarks for alpha target scenes.

### Vehicle Completion
- Differential behavior and wheel torque distribution polish.
- Better tire slip/grip model and tuning curves.
- Full tuning asset workflow in editor (author once, reuse across vehicles).
- Visual wheel sync polish (steer/spin/suspension) across all edge cases.
- Optional assists (traction control/ABS scaffolding) for later enablement.

### Destruction Completion (Major)
- Full Blast authoring pipeline from mesh -> chunk graph -> runtime asset.
- Reliable “chunk count/size” control exposed in editor and honored at runtime.
- Proper collision generation per chunk and stable post-fracture contacts.
- Material/visual handling for interior faces and chunk rendering quality.
- Runtime pooling/cleanup optimization for large fracture events.
- Event hooks for gameplay/audio/VFX/network replication.

### Tooling and QA
- Physics debug visualization pass cleanup and toggle consistency.
- Automated test scenes for vehicles + destruction.
- CI coverage for physics smoke + fracture stress tests.
- Final alpha documentation pass for setup, limits, and known issues.

## Suggested Alpha Exit Criteria
- Core rigid body/collider gameplay is stable in target sample scenes.
- Vehicle controller is drivable and tunable with no blocker bugs.
- Destruction supports configurable chunking and reliable collision.
- No high-severity physics regressions across play/stop cycles.
- Performance remains acceptable in physics-heavy test scenarios.
