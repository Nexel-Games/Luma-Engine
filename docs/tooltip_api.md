# Tooltip API (Editor UI)

This document describes the centralized tooltip framework used by the editor UI.

## Location

- Header: `include/Luma/Editor/UI/TooltipAPI.h`
- Source: `src/Editor/UI/TooltipAPI.cpp`

## Namespace

- `Luma::UI::Tooltip`

## Core API

- `SetDelay(float seconds)`
- `GetDelay()`
- `SetStyle(const TooltipStyle&)`
- `GetStyle()`
- `VisibleLabel(const char* imguiLabel)`
- `Show(std::string_view text)`
- `ShowForItemLabel(const char* imguiLabel, std::string_view prefix = {})`
- `ShowRich(const TooltipDesc& desc)`
- `ShowInteractive(const TooltipDesc& desc)` (currently routed to rich tooltip rendering)

## Data Types

### `TooltipStyle`

- Background/border/title/text colors
- Padding
- Corner radius
- Border size

### `TooltipDesc`

- `title`
- `description`
- `shortcut`
- `docLink`
- `previewTexture`
- `previewSize`

## Behavior

- Tooltip delay is centralized (default `0.4s`).
- Delay is tracked per hovered ImGui item.
- Styling is consistent across all editor panels.
- Rich tooltip supports title, body, shortcut hint, docs line, and preview image.

## Usage Examples

### Simple tooltip

```cpp
if (ImGui::Button("Play"))
{
    // ...
}
Luma::UI::Tooltip::Show("Play current scene.");
```

### Label-derived tooltip

```cpp
ImGui::Checkbox("VSync", &config.vsync);
Luma::UI::Tooltip::ShowForItemLabel("VSync", "Toggle ");
```

### Rich tooltip

```cpp
Luma::UI::TooltipDesc desc {};
desc.title = "Translate Tool";
desc.description = "Move selected entities.";
desc.shortcut = "W";
Luma::UI::Tooltip::ShowRich(desc);
```

## Integration in TriangleLayer

`TriangleLayer.cpp` uses tooltip-aware UI wrappers (for buttons, checkboxes, combos, text fields, sliders, etc.) so tooltip behavior is consistent and does not require ad-hoc per-widget code in each panel.

