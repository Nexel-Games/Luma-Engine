#include "Luma/Editor/Panels/Rendering/GPUResourcesPanel.h"

#include <cstdint>
#include <sstream>
#include <string>

#include <imgui.h>

namespace Luma::Editor
{
    namespace
    {
        const char* StreamStateLabel(const Assets::StreamState state)
        {
            switch (state)
            {
            case Assets::StreamState::Queued:
                return "Queued";
            case Assets::StreamState::Streaming:
                return "Streaming";
            case Assets::StreamState::Resident:
                return "Resident";
            case Assets::StreamState::Evicted:
                return "Evicted";
            case Assets::StreamState::Failed:
                return "Failed";
            case Assets::StreamState::Cancelled:
                return "Cancelled";
            }

            return "Unknown";
        }

        const char* StreamTypeLabel(const Assets::StreamResourceType type)
        {
            switch (type)
            {
            case Assets::StreamResourceType::Texture:
                return "Texture";
            case Assets::StreamResourceType::Mesh:
                return "Mesh";
            case Assets::StreamResourceType::Audio:
                return "Audio";
            case Assets::StreamResourceType::Buffer:
                return "Buffer";
            case Assets::StreamResourceType::PackageBlob:
                return "Package";
            case Assets::StreamResourceType::Procedural:
                return "Procedural";
            case Assets::StreamResourceType::Unknown:
            default:
                return "Unknown";
            }
        }

        std::string FormatStreamingBytes(const std::uint64_t bytes)
        {
            constexpr double kKilobyte = 1024.0;
            constexpr double kMegabyte = 1024.0 * 1024.0;
            constexpr double kGigabyte = 1024.0 * 1024.0 * 1024.0;

            std::ostringstream stream;
            stream.setf(std::ios::fixed, std::ios::floatfield);
            stream.precision(2);

            if (bytes >= static_cast<std::uint64_t>(kGigabyte))
            {
                stream << (static_cast<double>(bytes) / kGigabyte) << " GB";
            }
            else if (bytes >= static_cast<std::uint64_t>(kMegabyte))
            {
                stream << (static_cast<double>(bytes) / kMegabyte) << " MB";
            }
            else if (bytes >= static_cast<std::uint64_t>(kKilobyte))
            {
                stream << (static_cast<double>(bytes) / kKilobyte) << " KB";
            }
            else
            {
                stream << bytes << " B";
            }

            return stream.str();
        }
    }

    void GPUResourcesPanel::Draw(bool* open, const GPUResourcesPanelContext& context)
    {
        if (!ImGui::Begin("GPU Resources", open))
        {
            ImGui::End();
            return;
        }

        const GPUResourceManager::Stats& stats = context.stats;
        const std::size_t activeTotal =
            stats.renderPassCount +
            stats.pipelineCount +
            stats.meshCount +
            stats.textureCount +
            stats.renderTargetCount +
            stats.framebufferCount +
            stats.descriptorSetCount;

        ImGui::Text(
            "Active: %llu | Pending Destroy: %llu",
            static_cast<unsigned long long>(activeTotal),
            static_cast<unsigned long long>(stats.pendingDestroyCount));
        ImGui::TextDisabled(
            "RP:%llu  PS:%llu  Mesh:%llu  Tex:%llu  RT:%llu  FB:%llu  DS:%llu",
            static_cast<unsigned long long>(stats.renderPassCount),
            static_cast<unsigned long long>(stats.pipelineCount),
            static_cast<unsigned long long>(stats.meshCount),
            static_cast<unsigned long long>(stats.textureCount),
            static_cast<unsigned long long>(stats.renderTargetCount),
            static_cast<unsigned long long>(stats.framebufferCount),
            static_cast<unsigned long long>(stats.descriptorSetCount));

        if (stats.pendingDestroyCount > 96)
        {
            ImGui::Spacing();
            ImGui::TextColored(
                ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
                "Warning: deferred release queue is large (%llu).",
                static_cast<unsigned long long>(stats.pendingDestroyCount));
            ImGui::TextDisabled("Potential resource lifecycle leak or sustained churn detected.");
        }

        ImGui::Spacing();
        const ImGuiTableFlags tableFlags =
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp |
            ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("GPUResourcesTable", 6, tableFlags, ImVec2(0.0f, 0.0f)))
        {
            ImGui::TableSetupColumn("Handle", ImGuiTableColumnFlags_WidthFixed, 84.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 112.0f);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 92.0f);
            ImGui::TableSetupColumn("Created", ImGuiTableColumnFlags_WidthFixed, 84.0f);
            ImGui::TableSetupColumn("Retire", ImGuiTableColumnFlags_WidthFixed, 84.0f);
            ImGui::TableHeadersRow();

            for (const GPUResourceManager::DebugEntry& entry : context.entries)
            {
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(std::to_string(entry.handle).c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(entry.typeLabel.c_str());

                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(entry.debugName.c_str());

                ImGui::TableSetColumnIndex(3);
                if (entry.pendingDestroy)
                {
                    ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.34f, 1.0f), "Pending");
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.46f, 0.86f, 0.58f, 1.0f), "Live");
                }

                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(std::to_string(entry.createdFrame).c_str());

                ImGui::TableSetColumnIndex(5);
                if (entry.pendingDestroy)
                {
                    ImGui::TextUnformatted(std::to_string(entry.retireFrame).c_str());
                }
                else
                {
                    ImGui::TextDisabled("-");
                }
            }

            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Resource Streaming");

        const Assets::StreamingStats& streamingStats = context.streamingStats;
        const std::string residentCpuText = FormatStreamingBytes(streamingStats.residentCpuBytes);
        const std::string residentGpuText = FormatStreamingBytes(streamingStats.residentGpuBytes);
        const std::string budgetCpuText = FormatStreamingBytes(streamingStats.budget.maxCpuResidentBytes);
        const std::string budgetGpuText = FormatStreamingBytes(streamingStats.budget.maxGpuResidentBytes);

        ImGui::Text(
            "Queued: %u | In Flight: %u | Resident: %u | Evicted: %u | Failed: %u | Cancelled: %u",
            streamingStats.queuedCount,
            streamingStats.inFlightCount,
            streamingStats.residentCount,
            streamingStats.evictedCount,
            streamingStats.failedCount,
            streamingStats.cancelledCount);
        ImGui::TextDisabled(
            "CPU %s / %s | GPU %s / %s | Completed %llu | Evictions %llu",
            residentCpuText.c_str(),
            budgetCpuText.c_str(),
            residentGpuText.c_str(),
            budgetGpuText.c_str(),
            static_cast<unsigned long long>(streamingStats.completedRequestCount),
            static_cast<unsigned long long>(streamingStats.evictionCount));

        if (ImGui::CollapsingHeader("Streaming Records", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (context.streamingRecords.empty())
            {
                ImGui::TextDisabled("No streaming records are tracked yet.");
            }
            else if (ImGui::BeginTable("StreamingResourcesTable", 7, tableFlags, ImVec2(0.0f, 220.0f)))
            {
                ImGui::TableSetupColumn("Handle", ImGuiTableColumnFlags_WidthFixed, 72.0f);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 92.0f);
                ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 92.0f);
                ImGui::TableSetupColumn("LOD", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_WidthFixed, 88.0f);
                ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (const Assets::StreamRecord& record : context.streamingRecords)
                {
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(std::to_string(record.handle).c_str());

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(StreamTypeLabel(record.resourceType));

                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(record.key.c_str());

                    ImGui::TableSetColumnIndex(3);
                    if (record.state == Assets::StreamState::Failed)
                    {
                        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", StreamStateLabel(record.state));
                    }
                    else if (record.state == Assets::StreamState::Resident)
                    {
                        ImGui::TextColored(ImVec4(0.46f, 0.86f, 0.58f, 1.0f), "%s", StreamStateLabel(record.state));
                    }
                    else
                    {
                        ImGui::TextUnformatted(StreamStateLabel(record.state));
                    }

                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%u/%u", record.resolvedLod, record.targetLod);

                    ImGui::TableSetColumnIndex(5);
                    ImGui::TextUnformatted(FormatStreamingBytes(record.residentCpuBytes).c_str());

                    ImGui::TableSetColumnIndex(6);
                    const std::string sourceText =
                        record.resolvedSourcePath.empty() ? record.sourcePath.generic_string() : record.resolvedSourcePath.generic_string();
                    ImGui::TextUnformatted(sourceText.c_str());
                    if (!record.lastError.empty() && ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("%s", record.lastError.c_str());
                    }
                }

                ImGui::EndTable();
            }
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Editor Rendering");
        ImGui::Text(
            "Scene Cache Rebuild: %.2f ms | Scene View Build: %.2f ms | Render Frame: %.2f ms",
            context.sceneRebuildMs,
            context.sceneViewBuildMs,
            context.renderFrameMs);

        ImGui::Spacing();
        ImGui::SeparatorText("Content Thumbnails");

        const ContentBrowserThumbnailStats& thumbnailStats = context.thumbnailStats;
        const ThumbnailServiceStats& thumbnailServiceStats = thumbnailStats.serviceStats;
        ImGui::Text(
            "Entries: %llu total | %llu filtered | Cache: %llu",
            static_cast<unsigned long long>(thumbnailStats.totalEntryCount),
            static_cast<unsigned long long>(thumbnailStats.filteredEntryCount),
            static_cast<unsigned long long>(thumbnailServiceStats.cacheEntryCount));
        ImGui::Text(
            "Pending: %llu | Ready: %llu | Failed: %llu | Queue: %llu",
            static_cast<unsigned long long>(thumbnailServiceStats.pendingCount),
            static_cast<unsigned long long>(thumbnailServiceStats.readyCount),
            static_cast<unsigned long long>(thumbnailServiceStats.failedCount),
            static_cast<unsigned long long>(thumbnailServiceStats.pendingQueueDepth));
        ImGui::TextDisabled(
            "BG Jobs %llu | BG Results %llu | Issued %llu/%llu | Folder Icon %s",
            static_cast<unsigned long long>(thumbnailServiceStats.backgroundJobQueueDepth),
            static_cast<unsigned long long>(thumbnailServiceStats.backgroundResultQueueDepth),
            static_cast<unsigned long long>(thumbnailStats.newRequestsIssuedThisFrame),
            static_cast<unsigned long long>(thumbnailStats.maxNewRequestsPerFrame),
            thumbnailStats.folderThumbnailLoaded ? "Ready" : "Fallback");
        ImGui::TextDisabled(
            "Thumbnail Tick %.2f ms | Upload %.2f ms | Processed %llu | Uploaded %llu/%llu",
            thumbnailServiceStats.lastTickMs,
            thumbnailServiceStats.lastUploadMs,
            static_cast<unsigned long long>(thumbnailServiceStats.lastProcessedCount),
            static_cast<unsigned long long>(thumbnailServiceStats.lastUploadCount),
            static_cast<unsigned long long>(thumbnailServiceStats.maxUploadsPerTick));

        ImGui::End();
    }
}
