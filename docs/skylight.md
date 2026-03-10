# Sky Light and Skybox in LumaEngine

This document explains how the Sky Light is created, how sky data flows through the engine, and how that data drives skybox rendering.

## 1. Where the Sky Light is defined

Core sky authoring data lives in:

- `SceneSkyLightComponent` in `include/Luma/Scene/LightComponents.h`
- Runtime copy struct `SceneSkyRuntimeState` in `include/Luma/Scene/SceneRuntimeState.h`

`SceneSkyLightComponent` contains:

- mode selection (`Color`, `HDRI`, `Procedural`)
- environment map paths and IBL fields
- blending, AO, lower-hemisphere settings
- volumetric cloud controls (coverage, density, wind, quality, debug mode, weather map)

## 2. How a Sky Light entity is created

Sky Lights are created the same way as other scene entities:

- Scene hierarchy menu: `CreateSkyLight()` in `include/Luma/Editor/SceneHierarchyPanel.h`
- Inspector Add Component popup: adds `SceneSkyLightComponent` in `include/Luma/Editor/InspectorPanel.h`
- Default scene setup: `SetupDefaultLightEntities()` creates a "Sky Light" in `src/Luma/Editor/EditorLayer.cpp`

Each entity still has the standard base components (`IDComponent`, `RelationshipComponent`, `TransformComponent`, `TagComponent`) from `Scene::CreateEntity()`.

## 3. How the user controls the sky

Inspector UI (`include/Luma/Editor/InspectorPanel.h`) exposes all sky controls:

- General: enabled, intensity, type, cast shadows
- Color mode: sky color and color intensity
- HDRI mode: environment map path, rotation, diffuse/reflection intensity, rebuild flag
- Ambient/AO, hemisphere, blending/priority
- Real-time capture interval
- Volumetric cloud tuning (shape, wind, lighting, shadows, quality, debug)

Important HDRI trigger:

- Editing environment map or pressing "Rebuild IBL" sets `RebuildIBLRequested = true`.

## 4. Frame-time data flow (active path)

The active viewport path is in `ViewportPanel::OnImGuiRender()`:

1. `SubmitViewportSceneData()` scans scene entities.
2. It picks the highest-priority active `SceneSkyLightComponent`.
3. Sky fields are copied into `SceneSkyRuntimeState` and `LEEditorViewportSceneData::SkyState`.
4. `sceneData.SkyEnabled` is set when a sky light is found.
5. Data is sent to renderer via `Renderer::SetEditorViewportSceneData(sceneData)`.

In this active path, Sky Light currently influences submitted scene data and ambient values, but backend skybox drawing is not wired yet.

## 5. Legacy OpenGL skybox pipeline (implemented, currently bypassed)

A full skybox renderer exists in `include/Luma/Editor/ViewportPanel.h`:

- `CreateSkyboxShaderProgram()` loads `assets/shaders/editor/skybox.vert` + `skybox.frag`
- `InitializeSkyboxGeometry()` creates the sky cube VAO/VBO
- `LoadSkyEnvironmentTexture()` loads HDR/EXR through `SkyTextureLoader`
- `BuildSkyCubemapFromEquirect()` converts equirect map to cubemap
- `EnsureCloudWeatherTexture()` loads optional cloud weather map
- `RenderSkybox()` binds textures and pushes sky/cloud uniforms before drawing

How Sky Light controls skybox in this path:

- `GetSkyState()` values are sent to shader uniforms:
  - `Type -> u_SkyType`
  - `Color -> u_SkyColor`
  - `Intensity -> u_SkyIntensity`
  - `Rotation -> u_RotationDegrees`
  - `Blend -> u_SkyBlendFactor`
  - `Environment path -> u_EnvironmentMap/u_Skybox` sampling path
  - `Cloud* -> u_Cloud*` uniforms for volumetric cloud rendering

## 6. Current migration status (important)

The first block in `ViewportPanel::OnImGuiRender()` always submits through `Renderer` and returns early.
Because of that early return, the old panel-side OpenGL skybox draw block (where `RenderSkybox()` is called) is currently not executed.

Also:

- `engine/runtime/renderer/backends/OpenGLRendererBackend.cpp` currently renders submitted geometry only (no skybox logic).
- `engine/runtime/renderer/Renderer.cpp` RHI viewport path currently uses a simple color pipeline (`rhi_viewport_color`) and does not consume `SkyState` for skybox shading.

So today, sky authoring data is present and propagated, but the active backend path does not yet render the full skybox/cloud model.

## 7. Asset loading details

Sky texture loading is handled by `SkyTextureLoader`:

- `.hdr` via `stb_image` floating-point load
- `.exr` via TinyEXR
- values are sanitized/clamped before upload

Files:

- `include/Luma/Editor/SkyTextureLoader.h`
- `src/Luma/Editor/SkyTextureLoader.cpp`

## 8. Serialization

Sky Light data is fully serialized/deserialized in:

- `engine/runtime/scene/SceneSerializer.cpp`

This preserves sky type, HDR paths, blend values, AO flags, and cloud settings across scene save/load.
