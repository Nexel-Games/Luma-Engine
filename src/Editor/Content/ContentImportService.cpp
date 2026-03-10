#include "Luma/Editor/Content/ContentImportService.h"

#include <algorithm>

namespace Luma::Editor
{
    void ContentImportService::Tick(const ContentImportTickContext& context)
    {
        if (!m_State.active)
        {
            return;
        }

        if (!context.assetPipelineInitialized || context.importPipeline == nullptr)
        {
            if (context.setStatus)
            {
                context.setStatus("Import queue stopped: asset pipeline is unavailable.");
            }
            if (m_State.task != 0)
            {
                EditorTaskManager::SetProgress(m_State.task, 1.0f);
                EditorTaskManager::EndTask(m_State.task);
            }
            m_State.task = 0;
            m_State.active = false;
            m_State.pendingImports.clear();
            m_State.cursor = 0;
            return;
        }

        const std::size_t totalCount = m_State.pendingImports.size();
        if (totalCount == 0)
        {
            if (m_State.task != 0)
            {
                EditorTaskManager::SetProgress(m_State.task, 1.0f);
                EditorTaskManager::EndTask(m_State.task);
                m_State.task = 0;
            }
            m_State.active = false;
            return;
        }

        constexpr std::size_t kImportsPerTick = 2;
        std::size_t importedThisTick = 0;
        while (m_State.cursor < totalCount && importedThisTick < kImportsPerTick)
        {
            const PendingContentImport& pendingImport = m_State.pendingImports[m_State.cursor];
            if (m_State.task != 0)
            {
                EditorTaskManager::SetSubtask(
                    m_State.task,
                    "Importing " + std::to_string(m_State.cursor + 1) + "/" + std::to_string(totalCount) +
                        ": " + pendingImport.sourcePath.filename().string());
                const float progress = 0.04f + 0.90f * (
                    static_cast<float>(m_State.cursor) /
                    static_cast<float>(std::max<std::size_t>(totalCount, 1)));
                EditorTaskManager::SetProgress(m_State.task, progress);
            }

            Assets::ImportRequest importRequest {};
            importRequest.sourcePaths = { pendingImport.sourcePath };
            importRequest.targetDirectory = pendingImport.targetDirectory;
            importRequest.headless = true;
            importRequest.generateThumbnails = false;

            const Assets::ImportResult importResult = context.importPipeline->Import(importRequest);
            if (importResult.success)
            {
                if (importResult.skipped)
                {
                    ++m_State.skippedCount;
                }
                else
                {
                    ++m_State.importedCount;
                }
            }
            else
            {
                ++m_State.failedCount;
                if (m_State.firstError.empty())
                {
                    m_State.firstError = importResult.message;
                }
            }

            ++m_State.cursor;
            ++importedThisTick;
        }

        if (m_State.cursor < totalCount)
        {
            return;
        }

        if (m_State.task != 0)
        {
            EditorTaskManager::SetSubtask(m_State.task, "Refreshing content browser...");
            EditorTaskManager::SetProgress(m_State.task, 0.98f);
        }

        if (context.invalidateFolderTreeCache)
        {
            context.invalidateFolderTreeCache();
        }
        if (context.refreshEntries)
        {
            context.refreshEntries();
        }

        if (m_State.task != 0)
        {
            EditorTaskManager::SetProgress(m_State.task, 1.0f);
            EditorTaskManager::EndTask(m_State.task);
            m_State.task = 0;
        }

        if (context.setStatus)
        {
            std::string status =
                "Drag/drop import: " +
                std::to_string(m_State.importedCount) + " imported, " +
                std::to_string(m_State.skippedCount) + " skipped, " +
                std::to_string(m_State.failedCount) + " failed.";
            if (m_State.failedCount > 0 && !m_State.firstError.empty())
            {
                status += " First error: " + m_State.firstError;
            }
            context.setStatus(std::move(status));
        }

        m_State.pendingImports.clear();
        m_State.cursor = 0;
        m_State.importedCount = 0;
        m_State.skippedCount = 0;
        m_State.failedCount = 0;
        m_State.firstError.clear();
        m_State.active = false;
    }

    void ContentImportService::Reset()
    {
        if (m_State.active && m_State.task != 0)
        {
            EditorTaskManager::EndTask(m_State.task);
        }
        m_State = {};
    }

    std::string ContentImportService::BuildFooterStatus() const
    {
        if (!m_State.active)
        {
            return {};
        }

        const int totalCount = static_cast<int>(m_State.pendingImports.size());
        const int importedCount = m_State.importedCount + m_State.skippedCount + m_State.failedCount;
        return "Importing assets " + std::to_string(importedCount) + "/" + std::to_string(totalCount) + "...";
    }
}
