# Entity and Component Creation in LumaEngine

This document explains how entity and component creation currently works across the engine.

## 1. Two ECS Paths Exist Today

LumaEngine currently has two parallel ECS styles:

- Scene ECS (editor-facing, primary path)
  - Files: `include/Luma/Scene/*`, `engine/runtime/scene/*`
  - Uses `Luma::Scene` + `Luma::Entity` wrapper over `entt::registry`
  - Drives editor hierarchy, inspector, scene serialization, and viewport rendering

- World ECS (runtime lighting helper)
  - Files: `include/Luma/ECS/*`, `engine/runtime/ecs/*`
  - Uses `World` + `entt::registry` directly
  - Mainly used to sync ECS light components into `LightingSystem`

Most editor/game-object authoring uses the Scene ECS path.

## 2. Scene ECS Core Model

`Luma::Scene` owns:

- `entt::registry m_Registry`
- `std::unordered_map<UUID, EntityID> m_EntityByUUID`
- `SceneRuntimeState m_RuntimeState`

`Luma::Entity` is a thin handle wrapper around `entt::entity` and the registry.

It provides templated helpers:

- `AddComponent<T>(...)`
- `GetComponent<T>()`
- `HasComponent<T>()`
- `RemoveComponent<T>()`

## 3. What Happens on Entity Creation

### 3.1 Base creation

`Scene::CreateEntity()` always creates these default components:

- `IDComponent` (generated `UUID`)
- `RelationshipComponent` (parent/children links)
- `TransformComponent` (local + derived world transform)
- `TagComponent` (default name `"Entity"`)

It also inserts UUID into `m_EntityByUUID`.

`Scene::CreateEntity(const std::string& name)` calls the base creation, then sets `TagComponent::Tag`.

### 3.2 Creation from editor UI

`SceneHierarchyPanel` creates entities via menu actions (Empty, Cube, Plane, Sphere, lights).

Example (Cube):

1. `m_Scene->CreateEntity("Cube...")`
2. `AddComponent<MeshRendererComponent>()`
3. `AddComponent<MaterialComponent>()`
4. Assign primitive mesh and default color

### 3.3 Adding components to existing entities

`InspectorPanel` has an **Add Component** popup that conditionally adds missing components:

- `MeshRendererComponent`
- `MaterialComponent`
- `CameraComponent`
- `SceneDirectionalLightComponent`
- `ScenePointLightComponent`
- `SceneSpotLightComponent`
- `SceneSkyLightComponent`

## 4. Hierarchy and Transform Propagation

Hierarchy is stored by UUID, not by raw `entt::entity` handles:

- `RelationshipComponent::Parent` is a UUID
- `RelationshipComponent::Children` is `std::vector<UUID>`

Parenting operations:

- `Scene::SetParent(child, parent)`
  - Validates entities/components
  - Prevents parent cycles
  - Removes child from old parent
  - Adds child to new parent list
  - Marks child transform dirty

- `Scene::Unparent(child)`
  - Removes child from parent list
  - Clears parent UUID
  - Marks child transform dirty

World transforms are recomputed with `Scene::UpdateWorldTransforms()`:

- Traverses root entities recursively
- Builds local TRS matrix
- Multiplies by parent world matrix
- Decomposes into `WorldPosition`, `WorldRotation`, `WorldScale`

## 5. Component Data to Rendering

`ViewportPanel::SubmitViewportSceneData()` extracts scene components and builds `LEEditorViewportSceneData`.

Key extraction patterns:

- Meshes: `registry.view<TransformComponent, MeshRendererComponent>()`
- Lights: collected via `Renderer::GetLightingSystemAPI().CollectSceneLights(*m_Scene)`
- Sky: `registry.view<SceneSkyLightComponent>()` (highest-priority active sky chosen)

It fills:

- Vertex/index buffers
- Draw items (with cull mode + texture index)
- Camera matrices
- Directional/point/spot/tube/sky runtime lighting data

Then it calls:

- `Renderer::SetEditorViewportSceneData(sceneData)`

Renderer forwards this data:

- Legacy path -> backend directly
- RHI path -> stores data, uploads VB/IB, records draw pass

## 6. Serialization and Deserialization

`SceneSerializer::Serialize()`:

- Iterates entities via `registry.view<IDComponent>()`
- Writes only components that exist on each entity
- Stores to JSON `.scene`

`SceneSerializer::Deserialize()`:

1. `scene->Clear()`
2. For each JSON entity: `scene->CreateEntity()` (creates defaults)
3. Applies loaded UUID using `SetEntityUUID`
4. Restores relationship/transform/tag
5. Adds optional components if present (mesh/material/camera/lights)

This means base components always exist after load, while optional components are data-driven.

## 7. World ECS Path (Lighting Sync)

Separate from Scene ECS, `World` supports:

- `CreateEntity()` and `CreateEntity(name)`
- optional `Tag` name map lookup
- system-driven updates (`LightingECSSystem`)

`LightingECSSystem`:

- Reads light + transform components from `World` registry
- Syncs them into renderer `LightingSystem`
- Tracks created/removed lights

This path is runtime-lighting focused and is not scene serialization-backed.

## 8. Minimal Usage Example (Scene ECS)

```cpp
auto entity = scene->CreateEntity("MyEntity");

auto& transform = entity.GetComponent<TransformComponent>();
transform.Position = {0.0f, 1.0f, 0.0f};

auto& mesh = entity.AddComponent<MeshRendererComponent>();
mesh.MeshPtr = MeshFactory::CreatePrimitive(PrimitiveType::Cube);
mesh.UsePrimitive = true;
mesh.Primitive = static_cast<int>(PrimitiveType::Cube);

if (!entity.HasComponent<MaterialComponent>())
    entity.AddComponent<MaterialComponent>();
```

## 9. How to Add a New Scene Component Safely

1. Define the component struct under `include/Luma/Scene/`.
2. Add editor creation/edit UI in hierarchy/inspector if needed.
3. Serialize + deserialize it in `SceneSerializer.cpp`.
4. If renderer needs it, map it in `ViewportPanel::SubmitViewportSceneData()`.
5. Validate save/load and viewport behavior on both renderer backends.

## 10. Important Notes

- `include/Luma/ECS/Entity.h` is currently empty; Scene entity wrapper lives in `include/Luma/Scene/Scene.h`.
- Removing core components (`IDComponent`, `RelationshipComponent`, `TransformComponent`) can break scene assumptions.
- Parenting and lookup correctness depend on UUID map consistency (`SetEntityUUID`, `DestroyEntity`, `Clear`).
