#pragma once

#include <memory>
#include <string_view>

#include <vector>

#include "Luma/Physics/IPhysicsBackend.h"
#include "Luma/Physics/PhysicsTypes.h"

namespace Luma
{
    class Scene;

    class PhysicsSystem
    {
    public:
        PhysicsSystem() = default;
        ~PhysicsSystem();

        bool Initialize(const PhysicsSettings& settings);
        void Shutdown();

        void SetEnabled(bool enabled);
        bool IsEnabled() const;

        void Simulate(Scene& scene, float deltaTimeSeconds);
        std::uint32_t ConsumeSimulatedStepCount();
        void ConsumeEvents(std::vector<PhysicsEvent>& outEvents);

        const PhysicsSettings& GetSettings() const;
        PhysicsBackendType GetBackendType() const;
        std::string_view GetBackendName() const;
        bool IsInitialized() const;

    private:
        std::unique_ptr<IPhysicsBackend> m_Backend;
        PhysicsSettings m_Settings {};
        float m_Accumulator = 0.0f;
        std::uint32_t m_LastSimulatedStepCount = 0;
        bool m_Enabled = true;
        bool m_Initialized = false;
    };
}
