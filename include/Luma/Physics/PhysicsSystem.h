#pragma once

#include <memory>
#include <string_view>

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

        const PhysicsSettings& GetSettings() const;
        PhysicsBackendType GetBackendType() const;
        std::string_view GetBackendName() const;
        bool IsInitialized() const;

    private:
        std::unique_ptr<IPhysicsBackend> m_Backend;
        PhysicsSettings m_Settings {};
        float m_Accumulator = 0.0f;
        bool m_Enabled = true;
        bool m_Initialized = false;
    };
}
