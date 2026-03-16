#pragma once

#include <string_view>

namespace Luma::VehicleInputActions
{
    inline constexpr std::string_view Context = "Vehicle.Default";
    inline constexpr std::string_view Throttle = "Vehicle.Throttle";
    inline constexpr std::string_view Steer = "Vehicle.Steer";
    inline constexpr std::string_view Brake = "Vehicle.Brake";
    inline constexpr std::string_view Handbrake = "Vehicle.Handbrake";
    inline constexpr std::string_view Clutch = "Vehicle.Clutch";
    inline constexpr std::string_view GearUp = "Vehicle.GearUp";
    inline constexpr std::string_view GearDown = "Vehicle.GearDown";
    inline constexpr std::string_view Reset = "Vehicle.Reset";
}
