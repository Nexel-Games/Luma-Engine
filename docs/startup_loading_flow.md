# Startup Loading Flow

This document describes how the editor startup transition now behaves between the project browser and the main editor.

## Goals

- Keep the loading screen visible until startup is actually complete.
- Avoid exposing partially initialized editor panels behind the startup card.
- Force a valid initial window resize/present so the editor does not appear black on first launch.

## Current Behavior

When a project is opened from the project browser:

1. `EditorLayer` enters `EditorState::Startup`.
2. A staged boot sequence advances through:
   - `Bootstrap`
   - `CoreInit`
   - `ModuleLoad`
   - `AssetScan`
   - `ScriptCompile`
   - `RendererInit`
   - `ShaderCompile`
   - `ProjectLoad`
   - `EditorInit`
   - `Ready`
3. The startup overlay remains active and opaque during the full transition.
4. The editor is only revealed after the `Ready` phase has held for additional startup frames.

## Black-Window Fix

The project-browser to editor transition no longer relies on async native maximize behavior during startup.

Instead:

- the project browser remains the last rendered UI surface during startup handoff
- the main editor ImGui pass is not drawn behind the loading screen
- the startup overlay uses the splash image as the fullscreen background instead of a flat black blocker
- the window handoff uses an explicit work-area resize instead of repeated `glfwMaximizeWindow()` calls

During that one-time transition:

- the GLFW window is resized to the monitor work area only once for the editor handoff
- the current framebuffer size is queried
- the active render backend receives an explicit `OnResize(width, height)`
- a window event is posted so the first present is not waiting on a later manual resize/minimize/maximize action

This is the fix for the startup case where the editor window stayed black until the user manually minimized and restored it.

## Overlay Rules

The startup overlay now uses the splash image as the fullscreen background with a dark tint, so users do not see a flat black surface while the renderer transitions.

The startup state also no longer calls the main editor ImGui pass while the loading overlay is active.

That means:

- the editor may still be initializing internally
- the dock layout is built on the first visible editor frame instead of being drawn behind the loading screen
- the user only sees the startup splash/progress UI until the handoff is complete

## Main Code Paths

- `include/Luma/Layers/EditorLayer.h`
- `src/Layers/EditorLayer.cpp`

## Important Implementation Notes

- `SetMainWindowFullscreen(bool)` is now stateful and only performs a transition when the requested window mode actually changes.
- `EngineBootState::Ready` no longer drops the overlay immediately; it waits for extra ready frames before switching to `EditorState::MainEditor`.
- The startup overlay background is intentionally opaque so it behaves like a dedicated loading screen, not a translucent modal over an unfinished editor.
