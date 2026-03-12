# Lua Scripting

Last updated: March 12, 2026

## Overview

Luma ships with a runtime Lua scripting layer for gameplay, scene automation, and rapid iteration.

The current model is:

- one global Lua runtime
- one script instance per entity with a `LuaScriptComponent`
- hot reload for edited `.lua` files
- inspector-exposed `Script Properties`
- scene serialization for per-entity property overrides
- runtime physics callbacks

Lua is meant to sit on top of C++ engine systems. It is not the ownership layer for rendering, physics internals, asset streaming, or core ECS data.

## Script format

Each script should return a table.

```lua
local RotateY = {}

RotateY.Properties = {
    Speed = 90.0
}

function RotateY:OnCreate()
end

function RotateY:OnUpdate(dt)
end

function RotateY:OnDestroy()
end

return RotateY
```

Luma creates one runtime instance of that table per entity.

Each instance gets:

- `self.entity`
- `self.Properties`

## Supported lifecycle callbacks

These callbacks are currently supported:

- `OnCreate()`
- `OnEnable()`
- `OnStart()`
- `OnUpdate(dt)`
- `OnFixedUpdate(dt)`
- `OnLateUpdate(dt)`
- `OnDisable()`
- `OnDestroy()`

Physics callbacks:

- `OnCollisionEnter(other)`
- `OnCollisionStay(other)`
- `OnCollisionExit(other)`
- `OnTriggerEnter(other)`
- `OnTriggerExit(other)`

Notes:

- `OnStart()` runs after `OnCreate()` for enabled scripts.
- `OnFixedUpdate(dt)` runs from simulated physics steps.
- Hot reload recreates the script instance and re-runs the lifecycle entry path.

## Script Properties

Scripts can expose editable properties through `Properties`.

Simple values:

```lua
local Example = {}

Example.Properties = {
    Speed = 4.0,
    Enabled = true
}

return Example
```

Descriptor values:

```lua
local Example = {}

Example.Properties = {
    Speed = { type = "float", default = 4.0, min = 0.0, max = 20.0, tooltip = "Units per second" },
    Tint = { type = "color4", default = { 0.3, 0.7, 1.0, 1.0 } },
    Mode = { type = "enum", default = "Idle", options = { "Idle", "Move", "Attack" } },
    Target = { type = "entity", default = 0 },
    ImpactSound = { type = "asset", default = "Assets/Audio/hit.wav" }
}

return Example
```

Current property support:

- `bool`
- `int`
- `float`
- `string`
- `vec2`
- `vec3`
- `vec4`
- `color3`
- `color4`
- `enum`
- `entity`
- `asset`

Inspector edits are stored as per-entity overrides and merged into `self.Properties` at runtime.

## Hot reload

When a `.lua` source file changes:

- Luma invalidates cached script metadata
- the runtime recreates affected instances
- property overrides are preserved
- faulted instances can recover once the script becomes valid again

If a script throws at runtime, the editor should stay alive. The instance faults, the console reports the error, and the footer mirrors the newest console line.

## Core namespaces

The Lua API is organized under `Luma`.

Common namespaces:

- `Luma.Log`
- `Luma.Time`
- `Luma.Input`
- `Luma.Scene`
- `Luma.Entity`
- `Luma.Transform`
- `Luma.Renderer`
- `Luma.Material`
- `Luma.RigidBody`
- `Luma.Light`
- `Luma.Camera`
- `Luma.Collider`
- `Luma.Audio`
- `Luma.AudioSource`
- `Luma.AudioListener`

## Entity proxies

For common gameplay components, entity proxy helpers are available:

```lua
local material = Luma.Entity.GetMaterial(self.entity)
local body = Luma.Entity.GetRigidBody(self.entity)
local light = Luma.Entity.GetLight(self.entity)
local audio = Luma.Entity.GetAudioSource(self.entity)
local listener = Luma.Entity.GetAudioListener(self.entity)
```

These proxies expose the same operations as the related static namespace helpers, but with cleaner per-entity syntax.

## Audio examples

Global one-shot:

```lua
Luma.Audio.PlayOneShot("Assets/Audio/click.wav", {
    volume = 0.8,
    pitch = 1.0
})
```

Component-driven source:

```lua
local audio = Luma.Entity.GetAudioSource(self.entity)
if audio then
    audio:SetClip("Assets/Audio/click.wav")
    audio:SetVolume(0.8)
    audio:Play()
end
```

## Example scripts

Example Lua scripts live in:

- [assets/scripts/examples/RotateAndTint.lua](c:/Luma/assets/scripts/examples/RotateAndTint.lua)
- [assets/scripts/examples/TriggerOneShot.lua](c:/Luma/assets/scripts/examples/TriggerOneShot.lua)

## Automated smoke coverage

Luma now includes:

- `Luma.Smoke.OpenGL`
- `Luma.Smoke.LuaRuntime`

The Lua runtime smoke verifies:

- `OnCreate`
- `OnStart`
- `OnUpdate`
- `OnFixedUpdate`
- `OnLateUpdate`
- property override application
- fault on invalid hot reload
- recovery on valid hot reload
- `OnDestroy`

Run directly:

```powershell
c:\Luma\build-vs18\Debug\Luma.exe --lua-runtime-smoke-test
```
