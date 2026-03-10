#include "Luma/Editor/Core/EditorTaskManager.h"

#include <algorithm>
#include <vector>

#include <imgui.h>

#include "Luma/RHI/IRenderBackend.h"

namespace Luma
{
    namespace
    {
        struct EditorTaskState
        {
            EditorTaskHandle handle = 0;
            EditorTaskDesc desc {};
            float progress = 0.0f;
            bool finished = false;
            bool cancelRequested = false;
        };

        std::vector<EditorTaskState> g_Tasks;
        EditorTaskHandle g_NextHandle = 1;
        EditorTaskState* FindTaskMutable(const EditorTaskHandle handle)
        {
            for (EditorTaskState& task : g_Tasks)
            {
                if (task.handle == handle)
                {
                    return &task;
                }
            }
            return nullptr;
        }

        const EditorTaskState* FindTask(const EditorTaskHandle handle)
        {
            for (const EditorTaskState& task : g_Tasks)
            {
                if (task.handle == handle)
                {
                    return &task;
                }
            }
            return nullptr;
        }

        EditorTaskState* GetTopBlockingTask()
        {
            for (auto it = g_Tasks.rbegin(); it != g_Tasks.rend(); ++it)
            {
                if (!it->finished && it->desc.blocking)
                {
                    return &(*it);
                }
            }
            return nullptr;
        }

        void CleanupFinishedTasks()
        {
            g_Tasks.erase(
                std::remove_if(
                    g_Tasks.begin(),
                    g_Tasks.end(),
                    [](const EditorTaskState& task)
                    {
                        return task.finished;
                    }),
                g_Tasks.end());
        }

    }

    void EditorTaskManager::SetRenderBackend(IRenderBackend* backend)
    {
        (void)backend;
    }

    EditorTaskHandle EditorTaskManager::BeginTask(const EditorTaskDesc& desc)
    {
        CleanupFinishedTasks();

        EditorTaskState task;
        task.handle = g_NextHandle++;
        task.desc = desc;
        if (task.desc.title.empty())
        {
            task.desc.title = "Working...";
        }
        task.progress = 0.0f;
        g_Tasks.push_back(std::move(task));
        return g_Tasks.back().handle;
    }

    bool EditorTaskManager::SetTitle(const EditorTaskHandle handle, const std::string_view title)
    {
        EditorTaskState* task = FindTaskMutable(handle);
        if (task == nullptr)
        {
            return false;
        }

        task->desc.title = std::string(title);
        return true;
    }

    bool EditorTaskManager::SetSubtask(const EditorTaskHandle handle, const std::string_view subtask)
    {
        EditorTaskState* task = FindTaskMutable(handle);
        if (task == nullptr)
        {
            return false;
        }

        task->desc.subtask = std::string(subtask);
        return true;
    }

    bool EditorTaskManager::SetProgress(const EditorTaskHandle handle, const float progress01)
    {
        EditorTaskState* task = FindTaskMutable(handle);
        if (task == nullptr)
        {
            return false;
        }

        task->progress = std::clamp(progress01, 0.0f, 1.0f);
        return true;
    }

    bool EditorTaskManager::EndTask(const EditorTaskHandle handle)
    {
        EditorTaskState* task = FindTaskMutable(handle);
        if (task == nullptr)
        {
            return false;
        }

        task->progress = 1.0f;
        task->finished = true;
        return true;
    }

    bool EditorTaskManager::IsCancelRequested(const EditorTaskHandle handle)
    {
        const EditorTaskState* task = FindTask(handle);
        return task != nullptr && task->cancelRequested;
    }

    bool EditorTaskManager::HasBlockingTask()
    {
        return GetTopBlockingTask() != nullptr;
    }

    void EditorTaskManager::DrawOverlay()
    {
        EditorTaskState* task = GetTopBlockingTask();
        std::vector<EditorTaskState*> nonBlockingTasks;
        for (EditorTaskState& entry : g_Tasks)
        {
            if (!entry.finished && !entry.desc.blocking)
            {
                nonBlockingTasks.push_back(&entry);
            }
        }
        if (task == nullptr && nonBlockingTasks.empty())
        {
            CleanupFinishedTasks();
            return;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr)
        {
            return;
        }

        if (task != nullptr)
        {
            ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
            ImGuiWindowFlags blockerFlags =
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoDocking |
                ImGuiWindowFlags_NoNav |
                ImGuiWindowFlags_NoBackground;
            ImGui::Begin("##EditorTaskBlocker", nullptr, blockerFlags);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(
                viewport->Pos,
                ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
                IM_COL32(8, 10, 13, 178));
            ImGui::End();

            const ImVec2 cardSize(
                std::clamp(viewport->Size.x * 0.34f, 420.0f, 560.0f),
                task->desc.cancellable ? 220.0f : 188.0f);

            const ImVec2 cardPos(
                viewport->Pos.x + (viewport->Size.x - cardSize.x) * 0.5f,
                viewport->Pos.y + (viewport->Size.y - cardSize.y) * 0.5f);

            ImGui::SetNextWindowPos(cardPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(cardSize, ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 18.0f));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.11f, 0.13f, 0.97f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.29f, 0.40f, 0.56f, 0.70f));
            ImGuiWindowFlags cardFlags =
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoDocking;

            ImGui::Begin("##EditorTaskModal", nullptr, cardFlags);
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);

            ImGui::TextUnformatted(task->desc.title.c_str());
            ImGui::Spacing();
            if (!task->desc.subtask.empty())
            {
                ImGui::TextDisabled("%s", task->desc.subtask.c_str());
            }
            else
            {
                ImGui::TextDisabled("Please wait while the editor completes this task.");
            }

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.22f, 0.56f, 0.95f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.16f, 0.19f, 1.0f));
            ImGui::ProgressBar(task->progress, ImVec2(-1.0f, 11.0f));
            ImGui::PopStyleColor(2);

            ImGui::TextDisabled("%.0f%%", task->progress * 100.0f);

            if (task->desc.cancellable)
            {
                ImGui::Spacing();
                if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
                {
                    task->cancelRequested = true;
                }
            }

            ImGui::End();
        }

        float toastY = viewport->Pos.y + viewport->Size.y - 24.0f;
        int toastCount = 0;
        for (auto it = nonBlockingTasks.rbegin(); it != nonBlockingTasks.rend() && toastCount < 3; ++it, ++toastCount)
        {
            EditorTaskState* nonBlockingTask = *it;
            const ImVec2 toastSize(
                std::clamp(viewport->Size.x * 0.22f, 320.0f, 420.0f),
                nonBlockingTask->desc.cancellable ? 112.0f : 92.0f);
            toastY -= toastSize.y;

            const ImVec2 toastPos(
                viewport->Pos.x + viewport->Size.x - toastSize.x - 20.0f,
                toastY);
            toastY -= 12.0f;

            ImGui::SetNextWindowPos(toastPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(toastSize, ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.09f, 0.11f, 0.94f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.24f, 0.31f, 0.44f, 0.85f));
            ImGuiWindowFlags toastFlags =
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoDocking |
                ImGuiWindowFlags_NoNav;

            const std::string toastId = "##EditorTaskToast" + std::to_string(nonBlockingTask->handle);
            ImGui::Begin(toastId.c_str(), nullptr, toastFlags);
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);

            ImGui::TextUnformatted(nonBlockingTask->desc.title.c_str());
            if (!nonBlockingTask->desc.subtask.empty())
            {
                ImGui::TextDisabled("%s", nonBlockingTask->desc.subtask.c_str());
            }

            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.22f, 0.56f, 0.95f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.16f, 0.19f, 1.0f));
            ImGui::ProgressBar(nonBlockingTask->progress, ImVec2(-1.0f, 8.0f));
            ImGui::PopStyleColor(2);

            if (nonBlockingTask->desc.cancellable)
            {
                ImGui::Spacing();
                if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f)))
                {
                    nonBlockingTask->cancelRequested = true;
                }
            }

            ImGui::End();
        }

        CleanupFinishedTasks();
    }

    void EditorTaskManager::Reset()
    {
        g_Tasks.clear();
        g_NextHandle = 1;
    }
}
