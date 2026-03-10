# Luma Engine

Luma Engine is a real-time game engine focused on editor tooling, asset workflows, and modular runtime systems.

The current alpha target is `OpenGL` only.
Renderer-facing engine code still routes through backend abstractions such as `IRenderBackend`, `GPUResourceManager`, `RenderGraph`, and `RHIFactory` so additional renderer implementations can be plugged in later without reworking the editor or scene pipeline.

## Modules

- Renderer
- RHI
- Physics
- Scene
- Input
- Editor
