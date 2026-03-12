#include "Luma/Physics/PhysicsSystem.h"

#include <algorithm>
#include <utility>

#include "Luma/Core/Foundation/Logging.h"
#include "Physics/Backends/PhysXBackend.h"
#include "Luma/Scene/VehicleInputSystem.h"

namespace Luma
{
    namespace
    {
        VehicleInputSystem g_VehicleInputSystem;

        std::unique_ptr<IPhysicsBackend> CreateBackend(const PhysicsBackendType type)
        {
            switch (type)
            {
            case PhysicsBackendType::PhysX:
                return std::make_unique<PhysXBackend>();
            case PhysicsBackendType::None:
            default:
                return nullptr;
            }
        }
    }

    PhysicsSystem::~PhysicsSystem()
    {
        Shutdown();
    }

    bool PhysicsSystem::Initialize(const PhysicsSettings& settings)
    {
        Shutdown();
        m_Settings = settings;
        m_Accumulator = 0.0f;
        m_Enabled = true;

        m_Backend = CreateBackend(settings.backend);
        if (!m_Backend)
        {
            LUMA_LOG_WARN("Physics", "No physics backend selected. Physics simulation disabled.");
            m_Initialized = false;
            return false;
        }

        if (!m_Backend->Initialize(m_Settings))
        {
            LUMA_LOG_ERROR("Physics", "Failed to initialize physics backend.");
            m_Backend.reset();
            m_Initialized = false;
            return false;
        }

        m_Initialized = true;
        LUMA_LOG_INFO("Physics", "Initialized backend: " + std::string(m_Backend->GetName()));
        return true;
    }

    void PhysicsSystem::Shutdown()
    {
        if (m_Backend)
        {
            m_Backend->Shutdown();
            m_Backend.reset();
        }

        m_Accumulator = 0.0f;
        m_Initialized = false;
    }

    void PhysicsSystem::SetEnabled(const bool enabled)
    {
        m_Enabled = enabled;
    }

    bool PhysicsSystem::IsEnabled() const
    {
        return m_Enabled;
    }

    void PhysicsSystem::Simulate(Scene& scene, const float deltaTimeSeconds)
    {
        if (!m_Initialized || !m_Enabled || !m_Backend || deltaTimeSeconds <= 0.0f)
        {
            return;
        }

        g_VehicleInputSystem.UpdatePlayerInputs(scene);

        const float fixedStep = std::clamp(m_Settings.fixedTimeStep, 1.0e-4f, 0.5f);
        const std::uint32_t maxSubSteps = std::max<std::uint32_t>(1u, m_Settings.maxSubSteps);

        m_Accumulator += deltaTimeSeconds;
        std::uint32_t subSteps = 0;
        while (m_Accumulator >= fixedStep && subSteps < maxSubSteps)
        {
            m_Backend->Simulate(scene, fixedStep);
            m_Accumulator -= fixedStep;
            ++subSteps;
        }

        if (subSteps == maxSubSteps && m_Accumulator > fixedStep * 4.0f)
        {
            m_Accumulator = fixedStep;
        }
    }

    const PhysicsSettings& PhysicsSystem::GetSettings() const
    {
        return m_Settings;
    }

    PhysicsBackendType PhysicsSystem::GetBackendType() const
    {
        return m_Backend ? m_Backend->GetType() : PhysicsBackendType::None;
    }

    std::string_view PhysicsSystem::GetBackendName() const
    {
        return m_Backend ? m_Backend->GetName() : std::string_view("None");
    }

    bool PhysicsSystem::IsInitialized() const
    {
        return m_Initialized;
    }
}
