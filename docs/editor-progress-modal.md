# Editor Progress Modal

`EditorTaskManager` provides a blocking editor-only progress overlay for heavy tasks.

## API

```cpp
Luma::EditorTaskDesc task;
task.title = "Compiling Shaders...";
task.subtask = "Building CoreX pipeline...";
task.blocking = true;
task.cancellable = false;

Luma::EditorTaskHandle handle = Luma::EditorTaskManager::BeginTask(task);
Luma::EditorTaskManager::SetProgress(handle, 0.35f);
Luma::EditorTaskManager::SetSubtask(handle, "Compiling 12/48 shader variants...");
Luma::EditorTaskManager::EndTask(handle);
```

## Behavior

- Blocking task overlays the editor with:
  - dimmed background
  - centered modal card
  - title, subtask text, progress bar
  - optional cancel button (cancel request token)
- UI interaction is blocked while a blocking task is active.
- Overlay rendering is called from `EditorLayer::OnImGuiRender`.

## Current Integrations

- Startup transition into main editor.
- Opening a project.
- Creating a project.
- Renderer-restart project launch path.
