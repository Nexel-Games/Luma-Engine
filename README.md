# Luma Engine

Luma Engine is a modern, high-performance game engine designed for advanced 3D and interactive experiences from a unified development environment. It focuses on editor tooling, asset workflows, and modular runtime systems so teams can build games instead of rebuilding core technology.

The current alpha target is `OpenGL` only. Renderer-facing engine code still routes through backend abstractions such as `IRenderBackend`, `GPUResourceManager`, `RenderGraph`, and `RHIFactory` so additional renderer implementations can be plugged in later without reworking the editor or scene pipeline.

## Design Philosophy

- Modularity first
- Editor-centric development
- Modern C++ practices
- Cross-platform ready architecture
- Asset-driven workflow

## Target Audience

- Indie game developers
- Technical artists
- Engine developers
- Educational institutions

## Current Status

Luma Engine is in alpha development with working foundations for rendering, scene management, asset importing, and editor tooling. The codebase is actively evolving toward a stable editor-first OpenGL alpha.

## Key Capabilities

- Real-time 3D rendering with an OpenGL backend
- Entity component system
- Asset pipeline with importing and streaming
- Editor interface with content browser, scene editor, and inspector panels
- Physics integration
- Project management with templates and build profiles
- Modular architecture for future extensibility

## Modules

- Renderer
- RHI
- Physics
- Scene
- Input
- Editor
