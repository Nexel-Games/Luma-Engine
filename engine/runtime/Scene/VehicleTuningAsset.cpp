#include "Luma/Scene/VehicleTuningAsset.h"

#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>

#include "Luma/Core/App/Project.h"

namespace Luma
{
    namespace
    {
        using json = nlohmann::json;
    }

    bool VehicleTuningAssetCacheService::TryApply(const std::string& assetPath, VehicleComponent& inOutVehicle)
    {
        const std::filesystem::path resolvedPath = ResolveAssetPath(assetPath);
        if (resolvedPath.empty())
        {
            return false;
        }

        std::error_code errorCode;
        const bool exists = std::filesystem::exists(resolvedPath, errorCode);
        if (errorCode || !exists)
        {
            m_Cache.erase(resolvedPath.generic_string());
            return false;
        }

        const auto lastWriteTime = std::filesystem::last_write_time(resolvedPath, errorCode);
        if (errorCode)
        {
            return false;
        }

        const std::string cacheKey = resolvedPath.generic_string();
        if (auto it = m_Cache.find(cacheKey); it != m_Cache.end() && it->second.lastWriteTime == lastWriteTime)
        {
            if (!it->second.valid)
            {
                return false;
            }

            inOutVehicle = it->second.vehicle;
            return true;
        }

        VehicleComponent loadedVehicle = inOutVehicle;
        if (!LoadVehicleTuningAsset(resolvedPath, loadedVehicle))
        {
            CacheEntry& entry = m_Cache[cacheKey];
            entry.lastWriteTime = lastWriteTime;
            entry.valid = false;
            return false;
        }

        CacheEntry& entry = m_Cache[cacheKey];
        entry.lastWriteTime = lastWriteTime;
        entry.vehicle = loadedVehicle;
        entry.valid = true;
        inOutVehicle = loadedVehicle;
        return true;
    }

    void VehicleTuningAssetCacheService::Clear()
    {
        m_Cache.clear();
    }

    std::filesystem::path VehicleTuningAssetCacheService::ResolveAssetPath(const std::string& assetPath) const
    {
        if (assetPath.empty())
        {
            return {};
        }

        std::error_code errorCode;
        const auto tryPath = [&](const std::filesystem::path& candidate) -> std::filesystem::path
        {
            if (candidate.empty())
            {
                return {};
            }
            if (std::filesystem::exists(candidate, errorCode) && !errorCode)
            {
                return std::filesystem::weakly_canonical(candidate, errorCode);
            }
            errorCode.clear();
            return {};
        };

        std::filesystem::path path(assetPath);
        if (path.is_absolute())
        {
            return tryPath(path);
        }

        if (Project::IsLoaded())
        {
            if (std::filesystem::path resolved = tryPath(Project::GetAssetsPath() / path); !resolved.empty())
            {
                return resolved;
            }
            if (std::filesystem::path resolved = tryPath(Project::GetProjectRoot() / path); !resolved.empty())
            {
                return resolved;
            }
        }

        return tryPath(std::filesystem::current_path() / path);
    }

    bool VehicleTuningAssetCacheService::LoadVehicleTuningAsset(const std::filesystem::path& assetPath, VehicleComponent& outVehicle) const
    {
        std::ifstream input(assetPath);
        if (!input)
        {
            return false;
        }

        json root = json::parse(input, nullptr, true, true);
        const json* tuningJson = &root;
        if (const auto it = root.find("vehicleTuning"); it != root.end() && it->is_object())
        {
            tuningJson = &(*it);
        }
        if (!tuningJson->is_object())
        {
            return false;
        }

        outVehicle.engineTorque = tuningJson->value("engineTorque", outVehicle.engineTorque);
        outVehicle.idleRPM = tuningJson->value("idleRPM", outVehicle.idleRPM);
        outVehicle.maxRPM = tuningJson->value("maxRPM", outVehicle.maxRPM);
        outVehicle.reverseGearRatio = tuningJson->value("reverseGearRatio", outVehicle.reverseGearRatio);
        outVehicle.gearRatios = tuningJson->value("gearRatios", outVehicle.gearRatios);
        if (outVehicle.gearRatios.empty())
        {
            outVehicle.gearRatios.push_back(tuningJson->value("gearRatio", 3.5f));
        }
        outVehicle.differentialRatio = tuningJson->value("differentialRatio", outVehicle.differentialRatio);
        outVehicle.brakeForce = tuningJson->value("brakeForce", outVehicle.brakeForce);
        outVehicle.handbrakeForce = tuningJson->value("handbrakeForce", outVehicle.handbrakeForce);
        outVehicle.frontBrakeBias = tuningJson->value("frontBrakeBias", outVehicle.frontBrakeBias);
        outVehicle.frontDriveBias = tuningJson->value("frontDriveBias", outVehicle.frontDriveBias);
        outVehicle.tireFrictionScale = tuningJson->value("tireFrictionScale", outVehicle.tireFrictionScale);
        outVehicle.suspensionStiffness = tuningJson->value("suspensionStiffness", outVehicle.suspensionStiffness);
        outVehicle.suspensionDamping = tuningJson->value("suspensionDamping", outVehicle.suspensionDamping);
        outVehicle.suspensionTravel = tuningJson->value("suspensionTravel", outVehicle.suspensionTravel);
        outVehicle.maxSteerAngleDegrees = tuningJson->value("maxSteerAngleDegrees", outVehicle.maxSteerAngleDegrees);
        outVehicle.steerSensitivity = tuningJson->value("steerSensitivity", outVehicle.steerSensitivity);
        outVehicle.shiftUpRPM = tuningJson->value("shiftUpRPM", outVehicle.shiftUpRPM);
        outVehicle.shiftDownRPM = tuningJson->value("shiftDownRPM", outVehicle.shiftDownRPM);
        outVehicle.automaticTransmission = tuningJson->value("automaticTransmission", outVehicle.automaticTransmission);
        outVehicle.enableABS = tuningJson->value("enableABS", outVehicle.enableABS);
        outVehicle.enableTCS = tuningJson->value("enableTCS", outVehicle.enableTCS);
        outVehicle.ackermannSteering = tuningJson->value("ackermannSteering", outVehicle.ackermannSteering);
        return true;
    }
}
