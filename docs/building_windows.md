# Windows Build

This repository is set up to build the same way for every Windows developer checkout.

## Supported Toolchain

- Visual Studio 18 2026
- x64
- CMake 3.21+

## One Supported Configure Flow

From the repository root:

```powershell
cmake --preset windows-vs18-debug
cmake --build --preset windows-vs18-debug
ctest --preset windows-vs18-debug -R "Luma\.Smoke\.(OpenGL|LuaRuntime|AudioRuntime|SceneRuntime|PrefabRuntime)"
```

For release:

```powershell
cmake --preset windows-vs18-release
cmake --build --preset windows-vs18-release
```

## What Is Vendored

The repository includes the Windows-side runtime dependencies required by the supported build, including:

- `glfw`
- `glad`
- `PhysX`
- `Blast` runtime `.lib` and `.dll` outputs used by `LUMA_ENABLE_BLAST=ON`

The build validates those vendored files at configure time and fails with a direct message if the checkout is incomplete.

## Clean Reconfigure

If a machine previously configured the project with a different generator or stale cache, delete `build-vs18` and re-run the preset flow.

```powershell
Remove-Item -Recurse -Force .\build-vs18
cmake --preset windows-vs18-debug
cmake --build --preset windows-vs18-debug
```
