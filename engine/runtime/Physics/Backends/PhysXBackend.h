#pragma once

#include "Luma/Physics/IPhysicsBackend.h"

namespace Luma
{
    class PhysXBackend final : public IPhysicsBackend
    {
    public:
        std::string_view GetName() const override;
        PhysicsBackendType GetType() const override;

        bool Initialize(const PhysicsSettings& settings) override;
        void Shutdown() override;
        void Simulate(Scene& scene, float fixedDeltaTimeSeconds) override;

    private:
        PhysicsSettings m_Settings {};
        bool m_Initialized = false;
        bool m_UsingNativePhysX = false;
    };
}
