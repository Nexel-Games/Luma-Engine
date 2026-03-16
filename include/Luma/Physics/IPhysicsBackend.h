#pragma once

#include <string_view>

#include <vector>

#include "Luma/Physics/PhysicsTypes.h"

namespace Luma
{
    class Scene;

    class IPhysicsBackend
    {
    public:
        virtual ~IPhysicsBackend() = default;

        virtual std::string_view GetName() const = 0;
        virtual PhysicsBackendType GetType() const = 0;

        virtual bool Initialize(const PhysicsSettings& settings) = 0;
        virtual void Shutdown() = 0;
        virtual void Simulate(Scene& scene, float fixedDeltaTimeSeconds) = 0;
        virtual void ConsumeEvents(std::vector<PhysicsEvent>& outEvents) = 0;
    };
}
