# Content Browser Loading

This document explains how the Content Browser now loads folders and why large directories should feel more responsive.

## What changed

The Content Browser now uses two separate optimizations:

1. Folder contents are refreshed asynchronously.
2. The folder tree is built asynchronously and cached instead of being rebuilt from the filesystem every frame.
3. Thumbnail requests are queued progressively instead of being forced as urgent work for every visible tile.

That means opening a directory no longer blocks the UI while the engine scans and sorts the folder on the main thread.

## Folder content refresh

When you open a folder in the Content Browser:

1. Luma updates the current directory immediately.
2. A background refresh job scans that directory.
3. The UI shows `Loading folder...` until the scan completes.
4. The finished result is applied on the next editor update tick.

This keeps the editor responsive even when a folder contains many files.

## Folder tree behavior

The left-side `Folders` tree is no longer doing live directory traversal every frame.

Instead:

1. Luma schedules a background build for the active content root.
2. The cached tree is reused during normal UI rendering.
3. The cache is invalidated only when content roots change or the user performs a content mutation such as:
   - import completion
   - manual refresh
   - folder creation

This removes repeated recursive filesystem work from the ImGui draw path.

## What the user should expect

When browsing large folders:

- The editor should remain interactive while content loads.
- The right pane may briefly show `Loading folder...`.
- The left pane may briefly show `Loading folders...`.
- The left folder tree should stop stuttering when expanded repeatedly.

## Manual refresh

You can still force a rescan with:

- Content Browser empty-space menu -> `Refresh`
- Content Browser item menu -> `Refresh`
- Console command: `content.refresh`

Manual refresh also invalidates the folder tree cache so the browser reflects external filesystem changes.

## Current limits

This change removes expensive synchronous scanning from the draw loop, but there are still practical limits:

- Very large folders will still take time to scan in the background.
- Thumbnail generation can still take time after entries appear, especially for heavy asset types.
- External filesystem changes are not watched live yet; they appear after refresh or another cache invalidation event.

## Recommended next step

If further optimization is needed, the next correct step is to add filesystem watching for content roots so cache invalidation becomes event-driven instead of refresh-driven.
