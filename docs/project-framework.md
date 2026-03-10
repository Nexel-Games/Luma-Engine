# Project Framework API

`Luma::Project` now supports project lifecycle, settings, serialization, and build profiles.

## Core Types

- `Project::ProjectConfig`
  - General: `name`, `templateName`, `projectVersion`, `engineVersion`, `startScene`
  - Rendering: `pipeline`, `backend`, `vsync`
  - Build: `build.activeProfile`, plus `debug`, `development`, `release` profile configs
- `Project::BuildProfileConfig`
  - `enableValidation`
  - `enableOptimizations`
  - `enableDebugSymbols`
  - `enableHotReload`
  - `outputDirectory`
  - `defines`
- `BuildProfile`
  - `Debug`, `Development`, `Release`

## Project Creation

```cpp
Luma::Project::CreateProjectDesc desc;
desc.rootPath = "C:/MyProjects/MyGame";
desc.name = "MyGame";
desc.templateName = "Blank Project";
desc.config = Luma::Project::DefaultConfig(desc.name, desc.templateName);
desc.config.pipeline = Luma::RenderPipelineProfile::CoreX;
desc.config.backend = Luma::BackendPreference::OpenGL;

Luma::Project::Create(desc, nullptr);
```

Legacy creation (`Create(path, name)`) is still supported.

## Settings API

- `Project::UpdateSettings(config, saveImmediately)`
- `Project::SetRenderingConfig(pipeline, backend, vsync)`
- `Project::SetActiveBuildProfile(profile, saveImmediately)`
- `Project::GetBuildSettings()`
- `Project::GetActiveBuildProfile()`

## Config Serialization

- `Project::SerializeConfig(file, config)`
- `Project::DeserializeConfig(file, config)`

Config is stored in `.ep` with schema/versioned keys, including per-profile build settings:

- `Build.ActiveProfile`
- `Build.Debug.*`
- `Build.Development.*`
- `Build.Release.*`

Backward compatibility is preserved for older `.ep` files that only had basic keys.

## Build Paths

- `Project::GetBuildPath()` returns project build root.
- `Project::GetBuildPath(BuildProfile)` returns profile output directory.



.\build-vs18\Debug\Luma.exe --api=opengl

.\build-vs18\Debug\Luma.exe --api=opengl

.\build-vs18\Debug\Luma.exe --api=opengl
