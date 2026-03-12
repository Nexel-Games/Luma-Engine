#include "Luma/Editor/Panels/Inspector/InspectorDestructionPanel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>

#include "Luma/Core/App/Project.h"
#include "Luma/Editor/UI/TooltipAPI.h"
#include "Luma/Scene/DestructibleComponent.h"

namespace Luma::Editor
{
    namespace
    {
        void ShowItemTooltip(const std::string_view tooltip)
        {
            UI::Tooltip::Show(tooltip);
        }

        void ShowItemTooltipFromLabel(const char* label, const char* prefix = nullptr)
        {
            UI::Tooltip::ShowForItemLabel(label, prefix == nullptr ? std::string_view {} : std::string_view(prefix));
        }

        template <typename... Args>
        bool CheckboxWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::Checkbox(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Toggle ");
            return changed;
        }

        template <typename... Args>
        bool ButtonWithTooltip(const char* label, Args&&... args)
        {
            const bool pressed = ImGui::Button(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label);
            return pressed;
        }

        template <typename... Args>
        bool DragFloatWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::DragFloat(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Adjust ");
            return changed;
        }

        template <typename... Args>
        bool DragIntWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::DragInt(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Adjust ");
            return changed;
        }

        const char* ActivationModeLabel(const DestructionActivationMode mode)
        {
            switch (mode)
            {
            case DestructionActivationMode::StartFractured:
                return "Start Fractured";
            case DestructionActivationMode::StartIntact:
            default:
                return "Start Intact";
            }
        }

        const char* ChunkSizeLabel(const DestructionChunkSize size)
        {
            switch (size)
            {
            case DestructionChunkSize::Large:
                return "Large";
            case DestructionChunkSize::Small:
                return "Small";
            case DestructionChunkSize::Tiny:
                return "Tiny";
            case DestructionChunkSize::Medium:
            default:
                return "Medium";
            }
        }

        std::array<int, 3> GetChunkGridDims(const DestructionChunkSize size)
        {
            switch (size)
            {
            case DestructionChunkSize::Large:
                return { 2, 1, 1 };
            case DestructionChunkSize::Small:
                return { 2, 2, 2 };
            case DestructionChunkSize::Tiny:
                return { 4, 2, 2 };
            case DestructionChunkSize::Medium:
            default:
                return { 2, 2, 1 };
            }
        }

        int ComputeGridCellCount(const std::array<int, 3>& dims)
        {
            return std::max(1, dims[0]) * std::max(1, dims[1]) * std::max(1, dims[2]);
        }

        std::array<int, 3> BuildGridDimsForChunkCount(int chunkCount)
        {
            chunkCount = std::max(1, chunkCount);
            std::array<int, 3> dims { 1, 1, 1 };
            while (ComputeGridCellCount(dims) < chunkCount)
            {
                int axis = 0;
                if (dims[1] < dims[axis])
                {
                    axis = 1;
                }
                if (dims[2] < dims[axis])
                {
                    axis = 2;
                }
                ++dims[axis];
            }
            return dims;
        }

        std::array<int, 3> ResolveChunkGridDims(
            const DestructionChunkSize chunkSize,
            const int desiredChunkCount)
        {
            if (desiredChunkCount > 0)
            {
                return BuildGridDimsForChunkCount(desiredChunkCount);
            }
            return GetChunkGridDims(chunkSize);
        }

        bool WriteSampleBlastAsset(
            std::filesystem::path& outPath,
            const DestructionChunkSize chunkSize,
            const int desiredChunkCount)
        {
            if (!Project::IsLoaded())
            {
                return false;
            }

            const std::filesystem::path assetDirectory =
                Project::GetProjectRoot() / "Assets" / "Destruction";
            std::error_code ec;
            std::filesystem::create_directories(assetDirectory, ec);
            if (ec)
            {
                return false;
            }

            outPath = assetDirectory / "SampleCrate_Generated.lumablast";
            std::ofstream stream(outPath, std::ios::trunc);
            if (!stream.is_open())
            {
                return false;
            }

            constexpr std::array<float, 3> rootHalfExtents { 0.5f, 0.5f, 0.5f };
            const std::array<int, 3> dims = ResolveChunkGridDims(chunkSize, desiredChunkCount);
            const int requestedChunkCount = desiredChunkCount > 0 ? desiredChunkCount : ComputeGridCellCount(dims);
            const int chunkCount = std::clamp(requestedChunkCount, 1, ComputeGridCellCount(dims));
            const std::array<float, 3> chunkHalfExtents {
                std::max(0.02f, (rootHalfExtents[0] / static_cast<float>(dims[0])) * 0.95f),
                std::max(0.02f, (rootHalfExtents[1] / static_cast<float>(dims[1])) * 0.95f),
                std::max(0.02f, (rootHalfExtents[2] / static_cast<float>(dims[2])) * 0.95f)
            };

            const auto toChunkIndex = [dims](const int x, const int y, const int z)
            {
                return x + y * dims[0] + z * dims[0] * dims[1];
            };

            stream <<
R"({
  "chunks": [
)";

            const auto cellIndex = [dims](const int x, const int y, const int z)
            {
                return x + y * dims[0] + z * dims[0] * dims[1];
            };
            std::vector<int> cellToChunk(static_cast<std::size_t>(ComputeGridCellCount(dims)), -1);
            bool wroteChunk = false;
            int nextChunkId = 0;
            for (int z = 0; z < dims[2] && nextChunkId < chunkCount; ++z)
            {
                for (int y = 0; y < dims[1] && nextChunkId < chunkCount; ++y)
                {
                    for (int x = 0; x < dims[0] && nextChunkId < chunkCount; ++x)
                    {
                        const std::array<float, 3> normalizedCenter {
                            (static_cast<float>(x) + 0.5f) / static_cast<float>(dims[0]),
                            (static_cast<float>(y) + 0.5f) / static_cast<float>(dims[1]),
                            (static_cast<float>(z) + 0.5f) / static_cast<float>(dims[2])
                        };
                        const float centerX = -rootHalfExtents[0] + normalizedCenter[0] * (rootHalfExtents[0] * 2.0f);
                        const float centerY = -rootHalfExtents[1] + normalizedCenter[1] * (rootHalfExtents[1] * 2.0f);
                        const float centerZ = -rootHalfExtents[2] + normalizedCenter[2] * (rootHalfExtents[2] * 2.0f);
                        const float volume = std::max(
                            0.001f,
                            chunkHalfExtents[0] * chunkHalfExtents[1] * chunkHalfExtents[2] * 8.0f);

                        if (wroteChunk)
                        {
                            stream << ",";
                        }
                        wroteChunk = true;

                        stream <<
R"(
    {
      "centroid": [)" << centerX << ", " << centerY << ", " << centerZ << R"(],
                      "halfExtents": [)" << chunkHalfExtents[0] << ", " << chunkHalfExtents[1] << ", " << chunkHalfExtents[2] << R"(],
      "volume": )" << volume << R"(,
      "parent": -1,
      "support": true,
      "userData": )" << nextChunkId << R"(
    })";
                        cellToChunk[static_cast<std::size_t>(cellIndex(x, y, z))] = nextChunkId;
                        ++nextChunkId;
                    }
                }
            }

            stream << R"(
  ],
  "bonds": [)";

            bool wroteBond = false;
            const auto appendBond = [&](const int ax, const int ay, const int az, const int bx, const int by, const int bz)
            {
                const int chunkA = cellToChunk[static_cast<std::size_t>(cellIndex(ax, ay, az))];
                const int chunkB = cellToChunk[static_cast<std::size_t>(cellIndex(bx, by, bz))];
                if (chunkA < 0 || chunkB < 0)
                {
                    return;
                }
                const std::array<float, 3> centerA {
                    -rootHalfExtents[0] + ((static_cast<float>(ax) + 0.5f) / static_cast<float>(dims[0])) * (rootHalfExtents[0] * 2.0f),
                    -rootHalfExtents[1] + ((static_cast<float>(ay) + 0.5f) / static_cast<float>(dims[1])) * (rootHalfExtents[1] * 2.0f),
                    -rootHalfExtents[2] + ((static_cast<float>(az) + 0.5f) / static_cast<float>(dims[2])) * (rootHalfExtents[2] * 2.0f)
                };
                const std::array<float, 3> centerB {
                    -rootHalfExtents[0] + ((static_cast<float>(bx) + 0.5f) / static_cast<float>(dims[0])) * (rootHalfExtents[0] * 2.0f),
                    -rootHalfExtents[1] + ((static_cast<float>(by) + 0.5f) / static_cast<float>(dims[1])) * (rootHalfExtents[1] * 2.0f),
                    -rootHalfExtents[2] + ((static_cast<float>(bz) + 0.5f) / static_cast<float>(dims[2])) * (rootHalfExtents[2] * 2.0f)
                };
                std::array<float, 3> bondCenter {
                    (centerA[0] + centerB[0]) * 0.5f,
                    (centerA[1] + centerB[1]) * 0.5f,
                    (centerA[2] + centerB[2]) * 0.5f
                };
                std::array<float, 3> bondNormal { 0.0f, 0.0f, 0.0f };
                int normalAxis = 0;
                float maxDelta = std::abs(centerB[0] - centerA[0]);
                for (int axis = 1; axis < 3; ++axis)
                {
                    const float delta = std::abs(centerB[axis] - centerA[axis]);
                    if (delta > maxDelta)
                    {
                        normalAxis = axis;
                        maxDelta = delta;
                    }
                }
                bondNormal[normalAxis] = 1.0f;
                const float area = std::max(
                    0.001f,
                    chunkHalfExtents[(normalAxis + 1) % 3] *
                        chunkHalfExtents[(normalAxis + 2) % 3] * 4.0f);

                if (wroteBond)
                {
                    stream << ",";
                }
                wroteBond = true;

                stream <<
R"(
    {
      "chunks": [)" << chunkA << ", " << chunkB << R"(],
      "normal": [)" << bondNormal[0] << ", " << bondNormal[1] << ", " << bondNormal[2] << R"(],
      "centroid": [)" << bondCenter[0] << ", " << bondCenter[1] << ", " << bondCenter[2] << R"(],
      "area": )" << area << R"(,
      "userData": )" << (chunkA * 100 + chunkB) << R"(
    })";
            };

            for (int z = 0; z < dims[2]; ++z)
            {
                for (int y = 0; y < dims[1]; ++y)
                {
                    for (int x = 0; x < dims[0]; ++x)
                    {
                        if (x + 1 < dims[0])
                        {
                            appendBond(x, y, z, x + 1, y, z);
                        }
                        if (y + 1 < dims[1])
                        {
                            appendBond(x, y, z, x, y + 1, z);
                        }
                        if (z + 1 < dims[2])
                        {
                            appendBond(x, y, z, x, y, z + 1);
                        }
                    }
                }
            }

            stream << R"(
  ]
})";

            return stream.good();
        }
    }

    void InspectorDestructionPanel::Draw(const InspectorDestructionPanelContext& context)
    {
        if (context.scene == nullptr || context.selectedEntity == entt::null)
        {
            return;
        }

        auto& registry = context.scene->GetRegistry();
        if (!registry.valid(context.selectedEntity) || !registry.all_of<DestructibleComponent>(context.selectedEntity))
        {
            return;
        }

        auto& destructible = registry.get<DestructibleComponent>(context.selectedEntity);

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Destruction", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ShowItemTooltip("Blast-ready destruction settings for fracture, impact damage, and chunk behavior.");
            ImGui::PushID("DestructibleComponent");

            CheckboxWithTooltip("Active", &destructible.active);

            std::array<char, 512> blastAssetBuffer {};
            std::snprintf(blastAssetBuffer.data(), blastAssetBuffer.size(), "%s", destructible.blastAsset.c_str());
            if (ImGui::InputText("Blast Asset", blastAssetBuffer.data(), blastAssetBuffer.size()))
            {
                destructible.blastAsset = blastAssetBuffer.data();
            }
            ShowItemTooltipFromLabel("Blast Asset", "Edit ");

            if (Project::IsLoaded())
            {
                if (ButtonWithTooltip("Create Sample Blast Asset"))
                {
                    std::filesystem::path samplePath;
                    if (WriteSampleBlastAsset(samplePath, destructible.chunkSize, destructible.desiredChunkCount))
                    {
                        std::error_code ec;
                        const std::filesystem::path relativePath =
                            std::filesystem::relative(samplePath, Project::GetProjectRoot() / "Assets", ec);
                        destructible.blastAsset = ec ? samplePath.string() : relativePath.generic_string();
                    }
                }
                ShowItemTooltip("Create a simple sample .lumablast asset in Assets/Destruction and assign it to this destructible.");
            }

            std::array<char, 512> intactMeshBuffer {};
            std::snprintf(intactMeshBuffer.data(), intactMeshBuffer.size(), "%s", destructible.intactMeshOverride.c_str());
            if (ImGui::InputText("Intact Mesh Override", intactMeshBuffer.data(), intactMeshBuffer.size()))
            {
                destructible.intactMeshOverride = intactMeshBuffer.data();
            }
            ShowItemTooltipFromLabel("Intact Mesh Override", "Edit ");

            CheckboxWithTooltip("Visible Intact Mesh", &destructible.visibleIntactMesh);
            CheckboxWithTooltip("Fracture On Impact", &destructible.fractureOnImpact);
            CheckboxWithTooltip("Accumulate Damage", &destructible.accumulateDamage);
            CheckboxWithTooltip("World Support", &destructible.worldSupport);
            CheckboxWithTooltip("Stress Damage", &destructible.stressDamage);

            int activationMode = static_cast<int>(destructible.activationMode);
            const char* activationItems[] = { "Start Intact", "Start Fractured" };
            if (ImGui::Combo("Activation Mode", &activationMode, activationItems, IM_ARRAYSIZE(activationItems)))
            {
                destructible.activationMode = static_cast<DestructionActivationMode>(activationMode);
            }
            ShowItemTooltipFromLabel("Activation Mode", "Choose ");
            ImGui::TextDisabled("Resolved Mode: %s", ActivationModeLabel(destructible.activationMode));

            int chunkSize = static_cast<int>(destructible.chunkSize);
            const char* chunkSizeItems[] = { "Large", "Medium", "Small", "Tiny" };
            if (ImGui::Combo("Chunk Size", &chunkSize, chunkSizeItems, IM_ARRAYSIZE(chunkSizeItems)))
            {
                destructible.chunkSize = static_cast<DestructionChunkSize>(chunkSize);
            }
            ShowItemTooltipFromLabel("Chunk Size", "Choose ");
            ImGui::TextDisabled("Generated Size: %s", ChunkSizeLabel(destructible.chunkSize));
            DragIntWithTooltip("Chunk Count", &destructible.desiredChunkCount, 1.0f, 0, 512);
            ShowItemTooltipFromLabel("Chunk Count", "Set ");
            ImGui::TextDisabled("0 uses preset size. Values above 0 request an exact chunk count.");

            DragFloatWithTooltip("Damage Threshold", &destructible.damageThreshold, 0.1f, 0.0f, 100000.0f);
            DragFloatWithTooltip("Impact Damage Scale", &destructible.impactDamageScale, 0.01f, 0.0f, 1000.0f);
            DragFloatWithTooltip("Damage Spread", &destructible.damageSpread, 0.01f, 0.0f, 1000.0f);
            DragFloatWithTooltip("Chunk Mass Scale", &destructible.chunkMassScale, 0.01f, 0.001f, 1000.0f);
            DragFloatWithTooltip("Max Chunk Speed", &destructible.maxChunkSpeed, 0.1f, 0.0f, 100000.0f);
            DragFloatWithTooltip("Debris Lifetime", &destructible.debrisLifetime, 0.1f, 0.0f, 100000.0f);
            DragIntWithTooltip("Support Depth", &destructible.supportDepth, 1.0f, 0, 128);

            destructible.damageThreshold = std::max(0.0f, destructible.damageThreshold);
            destructible.impactDamageScale = std::max(0.0f, destructible.impactDamageScale);
            destructible.damageSpread = std::max(0.0f, destructible.damageSpread);
            destructible.chunkMassScale = std::max(0.001f, destructible.chunkMassScale);
            destructible.maxChunkSpeed = std::max(0.0f, destructible.maxChunkSpeed);
            destructible.debrisLifetime = std::max(0.0f, destructible.debrisLifetime);
            destructible.supportDepth = std::max(0, destructible.supportDepth);
            destructible.desiredChunkCount = std::max(0, destructible.desiredChunkCount);

            ImGui::PopID();
        }

        if (ButtonWithTooltip("Remove Destruction Component"))
        {
            registry.remove<DestructibleComponent>(context.selectedEntity);
        }
    }
}
