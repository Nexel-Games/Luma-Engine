# Audio System

Last updated: March 12, 2026

## Overview

Luma currently ships with a miniaudio-backed runtime audio system for:

- clip loading and caching
- 2D playback
- 3D playback
- scene `AudioSourceComponent`
- scene `AudioListenerComponent`
- editor preview playback from the inspector
- Lua-facing audio APIs

The current implementation is intended as a practical alpha audio foundation, not a full mixer/effects stack yet.

## Current architecture

Main runtime entry point:

- [AudioSystem.h](c:/Luma/include/Luma/Audio/Core/AudioSystem.h)
- [AudioSystem.cpp](c:/Luma/engine/runtime/Audio/Core/AudioSystem.cpp)

Scene components:

- [AudioSourceComponent.h](c:/Luma/include/Luma/Scene/AudioSourceComponent.h)
- [AudioListenerComponent.h](c:/Luma/include/Luma/Scene/AudioListenerComponent.h)

Editor inspector:

- [InspectorAudioPanel.cpp](c:/Luma/src/Editor/Panels/Inspector/InspectorAudioPanel.cpp)

Play-mode scene integration:

- [TriangleLayer.cpp](c:/Luma/src/Layers/TriangleLayer.cpp)

## Runtime behavior

### Audio clips

The runtime currently supports file-path-based clip loading and caching through `AudioSystem::LoadClip(...)`.

Supported practical formats right now:

- `.wav`
- `.ogg`
- `.mp3` may decode through miniaudio depending on build/runtime support, but `.wav` and `.ogg` are the intended alpha baseline

### Scene audio rules

Scene audio playback follows these rules:

- `AudioSourceComponent` can be attached to any entity
- `AudioListenerComponent` usually belongs on the active camera
- scene playback requires an enabled listener
- if no enabled listener exists, scene audio is muted and `Play On Awake` sources will not start
- when play mode stops, scene audio is fully stopped through `AudioSystem::StopAll()`

This is different from editor preview playback:

- inspector preview is editor-only
- it does not depend on the scene listener
- it is meant for quick clip auditioning outside play mode

## AudioSourceComponent

Current fields:

- `clipAsset`
- `playOnAwake`
- `looping`
- `spatialized`
- `mute`
- `volume`
- `pitch`
- `minDistance`
- `maxDistance`
- `runtimeHandle`

Intended use:

- looping ambience
- localized world SFX
- simple music/voice playback
- script-controlled playback on entities

## AudioListenerComponent

Current fields:

- `enabled`
- `volume`

Current behavior:

- first enabled listener found in the active scene is used
- if no listener is enabled, scene audio is muted
- new camera entities get an `AudioListenerComponent` by default

## Editor workflow

### Content Browser

Audio thumbnails currently use:

- [Audio_mp3.png](c:/Luma/assets/Images/Audio_mp3.png) for `.mp3`
- [Audio_Wav.png](c:/Luma/assets/Images/Audio_Wav.png) for `.wav` and `.lumaaudio`
- [Icon_Audio_Track_16x.png](c:/Luma/thirdparty/editor-icons/imgs/Sequencer/Dropdown_Icons/Icon_Audio_Track_16x.png) as the generic audio fallback

### Inspector preview

The `Audio Source` inspector currently supports:

- `Play Preview`
- `One Shot`
- `Stop Preview`

Preview playback is automatically cleaned up when:

- selection changes
- the clip changes
- the component is removed
- the preview voice ends

## Lua API

Global namespace:

- `Luma.Audio.IsInitialized()`
- `Luma.Audio.HasPlaybackDevice()`
- `Luma.Audio.Preload(path)`
- `Luma.Audio.Play2D(path[, settings])`
- `Luma.Audio.Play3D(path, position[, settings])`
- `Luma.Audio.PlayOneShot(path[, settings])`
- `Luma.Audio.Stop(handle)`
- `Luma.Audio.Pause(handle)`
- `Luma.Audio.Resume(handle)`
- `Luma.Audio.IsPlaying(handle)`
- `Luma.Audio.SetVolume(handle, volume)`
- `Luma.Audio.SetPitch(handle, pitch)`
- `Luma.Audio.SetPosition(handle, position)`
- `Luma.Audio.SetMasterVolume(volume)`
- `Luma.Audio.GetMasterVolume()`
- `Luma.Audio.SetListenerTransform(position[, forward[, up]])`

Entity component APIs:

- `Luma.AudioSource.*`
- `Luma.AudioListener.*`

Entity proxy helpers:

- `Luma.Entity.GetAudioSource(entity)`
- `Luma.Entity.GetAudioListener(entity)`

Example:

```lua
function Example:OnCreate()
    local audio = Luma.Entity.GetAudioSource(self.entity)
    if audio then
        audio:SetClip("Assets/Audio/click.wav")
        audio:SetVolume(0.8)
        audio:PlayOneShot()
    end
end
```

## Alpha scope

Supported audio scope for the current alpha track:

- 2D playback
- 3D playback
- source/listener scene components
- editor preview playback
- Lua control

Not yet treated as finished:

- bus/mixer groups
- effects
- reverb/ambient zones
- voice prioritization
- deeper streaming music pipeline
- dedicated audio regression suite

## Recommended authoring guidance right now

- use `.wav` for short SFX during current alpha work
- use `AudioSourceComponent` for scene-owned sounds
- use `Luma.Audio.PlayOneShot(...)` for fire-and-forget SFX
- keep one enabled `AudioListenerComponent` active during play mode

## Related examples

- [assets/scripts/examples/TriggerOneShot.lua](c:/Luma/assets/scripts/examples/TriggerOneShot.lua)
- [docs/lua_scripting.md](c:/Luma/docs/lua_scripting.md)
