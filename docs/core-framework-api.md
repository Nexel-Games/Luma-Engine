# Core Framework API

The core layer now provides engine-wide foundational systems:

- Memory management: `Luma::Memory`
- Logging: `Luma::Logger` + `LUMA_LOG_*`
- Assertions: `LUMA_ASSERT`, `LUMA_CORE_ASSERT`
- Time: `Luma::Time` (delta + fixed timestep consumption)
- UUID: `Luma::UUID`, `Luma::GenerateUUID()`
- Threading/Jobs: `Luma::JobSystem`
- Platform abstraction: `Luma::Platform`

## Headers

- `Luma/Core/Memory.h`
- `Luma/Core/Logging.h`
- `Luma/Core/Assert.h`
- `Luma/Core/Time.h`
- `Luma/Core/UUID.h`
- `Luma/Core/Jobs.h`
- `Luma/Core/Platform.h`
- `Luma/Core/Core.h` (aggregated include)

## Quick Usage

```cpp
#include "Luma/Core/Core.h"

Luma::Logger::Initialize();
Luma::Logger::SetLevel(Luma::LogLevel::Trace);

Luma::Time::Initialize(1.0 / 60.0);
Luma::JobSystem::Start();

const Luma::PlatformInfo& info = Luma::Platform::GetInfo();
LUMA_LOG_INFO("Core", "Platform: " + info.name);

Luma::Time::BeginFrame();
const double dt = Luma::Time::GetDeltaSeconds();

while (Luma::Time::ConsumeFixedStep())
{
    // fixed-step simulation tick
}

const Luma::UUID id = Luma::GenerateUUID();
LUMA_CORE_ASSERT(id != 0, "UUID generation failed.");
```

## Build Profiles

Build profile support is integrated in `ProjectConfig` through:

- `BuildProfile` (`Debug`, `Development`, `Release`)
- `BuildSettings` + per-profile `BuildProfileConfig`
