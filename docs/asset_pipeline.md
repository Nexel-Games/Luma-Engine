# Luma Asset Pipeline v1

## Purpose
This document explains the implemented **Luma Asset Framework v1** and how to use it end-to-end.

The implementation in this repository delivers:
- One backend-agnostic import pipeline for multiple asset classes.
- Deterministic import signatures (source hash + settings hash + importer signature).
- Reimport support that preserves per-asset import overrides.
- Plugin-style importer architecture (register importers in a registry).
- Cook pipeline scaffold for platform output.
- Headless CLI commands for import/reimport/cook/package install.

This v1 is designed as a foundation layer for your Unreal-like roadmap.

## Implemented Module Layout

### Core Asset Layer
- `include/Luma/Asset/Core/AssetTypes.h`
- `include/Luma/Asset/Core/AssetHash.h`
- `include/Luma/Asset/Core/AssetMeta.h`
- `include/Luma/Asset/Core/AssetMetaIO.h`
- `include/Luma/Asset/Core/AssetRegistry.h`
- `src/Asset/Core/*`

Responsibilities:
- Asset IDs and asset type model.
- Stable hashing for determinism.
- Metadata serialization (`.meta`).
- Asset registry indexing (`Assets/.asset_registry.json`).

### Import Framework Layer
- `include/Luma/Asset/Import/ImportSettings.h`
- `include/Luma/Asset/Import/Importer.h`
- `include/Luma/Asset/Import/ImporterRegistry.h`
- `include/Luma/Asset/Import/ImportPipeline.h`
- `include/Luma/Asset/Import/BuiltInImporters.h`
- `src/Asset/Import/*`

Responsibilities:
- Import request/result model.
- Importer plugin contract (`IAssetImporter`).
- Importer discovery/selection by priority.
- Pipeline orchestration and reimport logic.

### Cook Layer
- `include/Luma/Asset/Cook/AssetCooker.h`
- `src/Asset/Cook/AssetCooker.cpp`

Responsibilities:
- Read registry.
- Copy/imported assets into platform cook output.
- Write cook manifest.

### Package Layer
- `include/Luma/Asset/Package/PackageManager.h`
- `src/Asset/Package/PackageManager.cpp`

Responsibilities:
- Local package install into project cache.
- Lockfile update (`Packages/Packages.lock`).

### CLI Layer
- `include/Luma/Asset/CLI/AssetCLI.h`
- `src/Asset/CLI/AssetCLI.cpp`
- wired in `src/main.cpp`

Responsibilities:
- Headless command entrypoint for:
  - `import`
  - `reimport --changed`
  - `cook`
  - `packages install`

## Data Model and Files

## Asset Binary
Imported intermediate assets are written as type-specific files:
- `.lumatex`
- `.lumasky`
- `.lumamesh`
- `.lumaaudio`
- `.lumamat`
- `.lumaanim`
- `.lumaproc`
- `.lumapkg`
- fallback `.lumaasset`

The binary is backend-agnostic intermediate data (not API objects).

## Meta Sidecar
Each imported asset gets a sidecar:
- `<asset-file>.meta`

Format is key-value text and includes:
- `AssetID`
- `AssetType`
- `SourcePaths`
- `ImporterID`
- `ImporterVersion`
- `SourceHash`
- `SettingsHash`
- `BuildHash`
- `Setting.<key>=<value>` entries
- dependencies/tags/thumbnail fields

## Registry Index
- `Assets/.asset_registry.json`

Contains index entries to `.meta` files for fast startup/indexing.

If index is missing or stale, registry rebuild scans `Assets/**.meta`.

## Determinism and Caching Model

## Build Signature
For each import, the pipeline computes:
- `sourceHash` from source paths + source file bytes.
- `settingsHash` from sorted key/value settings.
- `buildHash` combining:
  - source hash
  - settings hash
  - importer ID
  - importer version

If an existing asset has the same `buildHash`, import is skipped.

## Reimport
`reimport --changed`:
- scans all registered assets,
- recomputes source hash,
- reimports only changed sources,
- preserves per-asset import settings from meta (`Setting.*`).

## Built-in Importers (v1)

Importers are registered by `RegisterBuiltInImporters(...)`.

Built-ins:
- `builtin.texture`: `.png .jpg .jpeg .tga .bmp .dds`
- `builtin.hdri`: `.hdr .exr`
- `builtin.model`: `.gltf .glb .obj .fbx`
- `builtin.audio`: `.wav .mp3 .ogg .flac`
- `builtin.procedural`: `.lpro .lumaproc .procjson`
- `builtin.package`: `.lpk .lumapkg .lpkmanifest`

Each importer defines:
- priority,
- extension handling (`CanHandle`),
- output asset type,
- settings schema (for editor-generated UI later),
- conversion to intermediate payload.

## CLI Usage

Binary:
- `C:\Luma\build-vs18\Debug\Luma.exe`

### Import
```powershell
.\build-vs18\Debug\Luma.exe import "C:\path\to\asset.png" --project "C:\MyProjects\MyNewProject\MyNewProject.ep" --target "Textures"
```

Optional:
- `--preset <name>`
- `--set key=value` (repeatable)

Example:
```powershell
.\build-vs18\Debug\Luma.exe import "C:\Luma\assets\Images\First_Person_Thumbnail.png" --project "C:\MyProjects\MyNewProject\MyNewProject.ep" --target "Textures" --set colorSpace=sRGB --set generateMipmaps=true
```

### Reimport Changed
```powershell
.\build-vs18\Debug\Luma.exe reimport --changed --project "C:\MyProjects\MyNewProject\MyNewProject.ep"
```

### Cook
```powershell
.\build-vs18\Debug\Luma.exe cook --platform windows --project "C:\MyProjects\MyNewProject\MyNewProject.ep"
```

Cook output:
- `Build/Cooked/<platform>/...`
- `Build/Cooked/<platform>/cooked_manifest.txt`

### Package Install (v1 local path flow)
```powershell
.\build-vs18\Debug\Luma.exe packages install "C:\path\to\SomePackage.lpk" --project "C:\MyProjects\MyNewProject\MyNewProject.ep"
```

Package output:
- `Packages/Cache/<package-file>`
- `Packages/Packages.lock`

## Editor Integration Notes

Current implementation is fully functional via CLI and registry/meta files.

Because the content browser already scans file system entries, imported assets appear as files in `Assets`.

The settings-schema-driven editor import dialog is prepared at API level (schema exists in importer contract), but full generated settings UI integration into the editor panel is a follow-up task.

### Drag-and-Drop Import in Content Browser
- File-drop from OS is captured through GLFW drop callbacks in the Input system.
- When files are dropped into the editor, the Content Browser consumes dropped paths and sends them through `ImportPipeline`.
- Dropped folders are scanned recursively and all regular files are queued for import.
- Import target defaults to the currently opened Content Browser directory.
- Dropped folder hierarchy is preserved (subfolders are not flattened).
- Source files are kept in folders during import:
  - files already under `Assets/` are imported in place
  - files dropped from outside the project are copied into target folders first, then imported
- The Editor Task Manager modal is shown during queued drag/drop imports with progress/subtask updates.
- Imports are processed in a queue across frames (instead of one blocking UI pass), so loader visibility is reliable.
- Result summary is shown in Content Browser status text:
  - imported count
  - skipped count
  - failed count (+ first error message)

## Adding a Custom Importer Plugin

1. Create a class implementing `IAssetImporter`.
2. Define:
   - `GetImporterID()`
   - `GetVersion()`
   - `GetPriority()`
   - `CanHandle(...)`
   - `GetOutputType(...)`
   - `BuildSettingsSchema()`
   - `Import(...)`
3. Register it through `ImporterRegistry::RegisterImporter(...)`.
4. Ensure deterministic output:
   - stable settings defaults,
   - sorted map use for metadata,
   - version bump when conversion behavior changes.

Minimal skeleton:
```cpp
class MyImporter final : public Luma::Assets::IAssetImporter
{
public:
    std::string_view GetImporterID() const override { return "plugin.my_importer"; }
    std::uint32_t GetVersion() const override { return 1; }
    int GetPriority() const override { return 200; }
    bool CanHandle(const ImportRequest& request) const override;
    AssetType GetOutputType(const ImportRequest& request) const override;
    ImportSettingsSchema BuildSettingsSchema() const override;
    bool Import(const ImportRequest& request,
                const ImportContext& context,
                const ImportSettingsMap& resolvedSettings,
                ImportOutput& outOutput,
                std::string& outError) const override;
};
```

## Current v1 Scope vs Roadmap

Implemented now:
- Asset registry/index/meta.
- Deterministic hash/signature model.
- Reimport on changed source.
- Importer plugin framework and built-ins.
- Cook command scaffold.
- Package install scaffold.
- CLI automation entrypoint.

Planned follow-up milestones:
- Full editor import popup generated from `ImportSettingsSchema`.
- Rich intermediate formats per asset class (real mesh/material/animation structures).
- Thumbnail renderer integration in asset pipeline stages.
- Dependency graph visualization and validation rules.
- Package dependency resolver against remote registry.
- Platform-specific texture/audio/mesh compression in cooker.
- CI golden-file tests and validation gates.

## Troubleshooting

### "No importer available for source"
Cause:
- extension not handled by registered importers.

Fix:
- add/register a custom importer or use supported extension.

### "Skipped import (source and settings unchanged)"
Cause:
- deterministic signature matched previous import.

Fix:
- change source file, change a setting, or bump importer version when conversion logic changes.

### Registry missing/empty
Cause:
- no imported assets yet, or index missing.

Fix:
- import at least one asset, or run reimport/import to regenerate `.meta` and index.

### Cook outputs missing
Cause:
- no registered/imported assets or asset files deleted.

Fix:
- reimport and re-run cook.

## Practical Workflow for Teams

1. Artists/TDs import through CLI or future editor dialog.
2. Commit:
   - generated asset binaries (`.lumatex/.lumamesh/...`)
   - sidecar `.meta`
   - optional registry index.
3. Teammates can run:
   - `reimport --changed` after source updates.
   - `cook --platform <target>` in CI.
4. Keep importer versions stable and bump version on behavior changes.

This ensures reproducible outputs and safer multi-user collaboration.
