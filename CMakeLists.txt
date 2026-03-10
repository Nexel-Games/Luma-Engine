cmake_minimum_required(VERSION 3.21)

project(Luma LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LUMA_ENABLE_PHYSX "Enable native PhysX backend integration" OFF)

set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/assimp EXCLUDE_FROM_ALL)

add_executable(
    Luma
    src/main.cpp
    engine/runtime/Core/App/Application.cpp
    engine/runtime/Core/App/LayerStack.cpp
    engine/runtime/Core/App/Project.cpp
    engine/runtime/Core/App/RenderPipeline.cpp
    engine/runtime/Core/App/RenderSelection.cpp
    engine/runtime/Core/Foundation/Logging.cpp
    engine/runtime/Core/Foundation/Jobs.cpp
    engine/runtime/Core/Foundation/Memory.cpp
    engine/runtime/Core/Foundation/Platform.cpp
    engine/runtime/Core/Foundation/Time.cpp
    engine/runtime/Core/Foundation/UUID.cpp
    engine/runtime/Core/Foundation/DynamicLibrary.cpp
    engine/runtime/Core/Foundation/StbImageWriteImpl.cpp
    engine/runtime/Asset/Core/AssetTypes.cpp
    engine/runtime/Asset/Core/AssetHash.cpp
    engine/runtime/Asset/Core/AssetMetaIO.cpp
    engine/runtime/Asset/Core/MeshAssetIO.cpp
    engine/runtime/Asset/Core/AssetRegistry.cpp
    engine/runtime/Asset/Import/ImporterRegistry.cpp
    engine/runtime/Asset/Import/ImportPipeline.cpp
    engine/runtime/Asset/Import/BuiltInImporters.cpp
    engine/runtime/Asset/Streaming/FileResourceStreamProvider.cpp
    engine/runtime/Asset/Streaming/ResourceStreamingService.cpp
    engine/runtime/Asset/Cook/AssetCooker.cpp
    engine/runtime/Asset/Package/PackageManager.cpp
    engine/runtime/Asset/Package/PackageSemVer.cpp
    engine/runtime/Asset/CLI/AssetCLI.cpp
     src/Editor/Content/ContentBrowserCache.cpp
     src/Editor/Content/ContentBrowserController.cpp
     src/Editor/Content/ContentBrowserHostFacadeService.cpp
     src/Editor/Content/ContentBrowserHostService.cpp
     src/Editor/Content/ContentThumbnailHostService.cpp
     src/Editor/Content/ContentImportService.cpp
     src/Editor/Content/ContentBrowserRefreshService.cpp
     src/Editor/Content/ContentBrowserRootService.cpp
     src/Editor/Console/ConsoleCommandHostService.cpp
     src/Editor/Console/ConsoleCommandService.cpp
     src/Editor/Console/ConsolePanelHostService.cpp
     src/Editor/Core/EditorDockspaceService.cpp
     src/Editor/Core/EditorIconService.cpp
     src/Editor/Core/GameplayInputBindingService.cpp
     src/Editor/Core/EditorTickCoordinatorService.cpp
     src/Editor/Scene/EntityTemplateCreationService.cpp
     src/Editor/Scene/SceneEntityUtilityService.cpp
     src/Editor/Viewport/ViewportAssetDropService.cpp
     src/Editor/Panels/Console/CommandPalettePanel.cpp
     src/Editor/Panels/Console/ConsolePanel.cpp
     src/Editor/Panels/Console/ConsoleOutputPanel.cpp
     src/Editor/Panels/Console/ConsoleTasksPanel.cpp
     src/Editor/Viewport/EditorViewportDebugOverlay.cpp
     src/Editor/Viewport/EditorViewportInteraction.cpp
     src/Editor/Viewport/EditorViewportController.cpp
     src/Editor/Core/EditorTaskManager.cpp
     src/Editor/Assets/MaterialTextureAssetPickerService.cpp
     src/Editor/Assets/MeshEntityImportService.cpp
     src/Editor/Assets/MeshStreamingGeometryService.cpp
     src/Editor/Assets/MeshAssetPickerService.cpp
     src/Editor/Packages/PackageManagerHostFacadeService.cpp
     src/Editor/Packages/PackageManagerHostService.cpp
     src/Editor/Rendering/MaterialRenderProxyCacheService.cpp
     src/Editor/Rendering/PostProcessBlendService.cpp
     src/Editor/Rendering/RenderFrameCoordinatorService.cpp
     src/Editor/Panels/Assets/MaterialTextureAssetPickerPanel.cpp
     src/Editor/Panels/Assets/MeshAssetPickerPanel.cpp
     src/Editor/Panels/Chrome/EditorMenuBarPanel.cpp
     src/Editor/Panels/Chrome/EditorToolbarPanel.cpp
     src/Editor/Panels/Scene/EntityCreationMenu.cpp
     src/Editor/Panels/Chrome/FooterBarPanel.cpp
     src/Editor/Panels/Rendering/GPUResourcesPanel.cpp
     src/Editor/Scene/SceneActionService.cpp
     src/Editor/Scene/SceneActionHostService.cpp
     src/Editor/Scene/InspectorHostService.cpp
     src/Editor/Scene/SceneFileService.cpp
     src/Editor/Scene/SceneBootstrapService.cpp
     src/Editor/Scene/SceneDocumentHostService.cpp
     src/Editor/Scene/SceneStartupHostService.cpp
     src/Editor/Rendering/SceneRenderCacheBuilder.cpp
     src/Editor/Rendering/SceneRenderCacheStateService.cpp
     src/Editor/Rendering/SceneRenderItemAssemblyService.cpp
     src/Editor/Rendering/SkyPreviewTextureHostService.cpp
     src/Editor/Rendering/SkyEnvironmentService.cpp
     src/Editor/Scene/SceneActionPrompt.cpp
     src/Editor/Scene/SceneDocument.cpp
     src/Editor/Panels/Inspector/InspectorAddComponentPanel.cpp
     src/Editor/Panels/Inspector/InspectorAdvancedPhysicsPanel.cpp
     src/Editor/Panels/Inspector/InspectorCameraLightingPanel.cpp
     src/Editor/Panels/Content/ContentBrowserPanel.cpp
     src/Editor/Panels/Scene/HierarchyPanel.cpp
     src/Editor/Panels/Inspector/InspectorEntityPanel.cpp
     src/Editor/Panels/Inspector/InspectorEnvironmentEffectsPanel.cpp
     src/Editor/Panels/Inspector/InspectorFieldBuoyancyPanel.cpp
     src/Editor/Panels/Inspector/InspectorJointPanel.cpp
     src/Editor/Panels/Inspector/InspectorMaterialPanel.cpp
     src/Editor/Panels/Inspector/InspectorMeshRendererPanel.cpp
     src/Editor/Panels/Inspector/InspectorPanel.cpp
     src/Editor/Panels/Inspector/InspectorPhysicsEventsPanel.cpp
     src/Editor/Panels/Inspector/InspectorPhysicsPanel.cpp
     src/Editor/Panels/Inspector/InspectorVehiclePhysicsPanel.cpp
     src/Editor/Panels/Project/PreferencesPanel.cpp
     src/Editor/Panels/Project/PluginsPanel.cpp
     src/Editor/Panels/Project/ProjectSettingsPanel.cpp
     src/Editor/Panels/Viewport/ViewportPanel.cpp
     src/Editor/Panels/Viewport/ViewportToolbarPanel.cpp
     src/Editor/Rendering/SceneViewBuilder.cpp
     src/Editor/Panels/Packages/PackageManagerPanel.cpp
    src/Editor/Plugins/BuiltInPluginRegistry.cpp
    src/Editor/UI/TooltipAPI.cpp
    src/Editor/Assets/ThumbnailService.cpp
    engine/runtime/Input/Input.cpp
    engine/runtime/Physics/PhysicsSystem.cpp
    engine/runtime/Physics/Backends/PhysXBackend.cpp
    engine/runtime/Renderer/PrimitiveMeshFactory.cpp
    src/Layers/EditorLayer.cpp
    src/Layers/ProjectManagerLayer.cpp
    src/Layers/TriangleLayer.cpp
    engine/runtime/Scene/Scene.cpp
    engine/runtime/Scene/SceneSerializer.cpp
    engine/runtime/RHI/RHIFactory.cpp
    engine/runtime/RHI/TextureSystem.cpp
    engine/runtime/RHI/LightingSystem.cpp
    engine/runtime/RHI/ShaderSystem.cpp
    engine/runtime/RHI/RenderGraph.cpp
    engine/runtime/RHI/GPUResourceManager.cpp
    engine/runtime/RHI/OpenGLRenderBackend.cpp
    thirdparty/imgui/imgui.cpp
    thirdparty/imgui/imgui_draw.cpp
    thirdparty/imgui/imgui_tables.cpp
    thirdparty/imgui/imgui_widgets.cpp
    thirdparty/imgui/backends/imgui_impl_glfw.cpp
    thirdparty/imgui/backends/imgui_impl_opengl3.cpp
    thirdparty/ImGuizmo/ImGuizmo.cpp
)

target_include_directories(
    Luma
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/engine/runtime
        ${CMAKE_SOURCE_DIR}/thirdparty/glfw/include
        ${CMAKE_SOURCE_DIR}/thirdparty/glad/include
        ${CMAKE_SOURCE_DIR}/thirdparty/PhysX/flow/external
        ${CMAKE_SOURCE_DIR}/thirdparty/PhysX/physx/include
        ${CMAKE_SOURCE_DIR}/thirdparty/imgui
        ${CMAKE_SOURCE_DIR}/thirdparty/imgui/backends
        ${CMAKE_SOURCE_DIR}/thirdparty/ImGuizmo
        ${CMAKE_SOURCE_DIR}/thirdparty
        ${CMAKE_SOURCE_DIR}/thirdparty/stb/include
        ${CMAKE_SOURCE_DIR}/thirdparty/assimp/contrib/zip/src
        ${CMAKE_SOURCE_DIR}/thirdparty/entt/src
)

set(GLFW_LIB_DEBUG "${CMAKE_SOURCE_DIR}/thirdparty/glfw/debug/lib/glfw3dll.lib")
set(GLFW_LIB_RELEASE "${CMAKE_SOURCE_DIR}/thirdparty/glfw/lib/glfw3dll.lib")
set(GLAD_LIB_DEBUG "${CMAKE_SOURCE_DIR}/thirdparty/glad/debug/lib/glad.lib")
set(GLAD_LIB_RELEASE "${CMAKE_SOURCE_DIR}/thirdparty/glad/lib/glad.lib")

target_link_libraries(
    Luma
    PRIVATE
        assimp::assimp
        debug ${GLFW_LIB_DEBUG}
        optimized ${GLFW_LIB_RELEASE}
        debug ${GLAD_LIB_DEBUG}
        optimized ${GLAD_LIB_RELEASE}
        comdlg32
        opengl32
)

target_compile_definitions(
    Luma
    PRIVATE
        IMGUI_IMPL_OPENGL_LOADER_GLAD
        LUMA_ENABLE_PHYSX=$<BOOL:${LUMA_ENABLE_PHYSX}>
)

if(MSVC)
    target_compile_options(Luma PRIVATE /FS)
endif()

set(GLFW_DLL_DEBUG "${CMAKE_SOURCE_DIR}/thirdparty/glfw/debug/bin/glfw3.dll")
set(GLFW_DLL_RELEASE "${CMAKE_SOURCE_DIR}/thirdparty/glfw/bin/glfw3.dll")

add_custom_command(
    TARGET Luma POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${GLFW_DLL_DEBUG},${GLFW_DLL_RELEASE}>"
            "$<TARGET_FILE_DIR:Luma>/glfw3.dll"
)

add_custom_command(
    TARGET Luma POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:assimp>"
            "$<TARGET_FILE_DIR:Luma>/$<TARGET_FILE_NAME:assimp>"
)

add_custom_command(
    TARGET Luma POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets/shaders"
            "$<TARGET_FILE_DIR:Luma>/shaders"
)

if(BUILD_TESTING)
    add_test(
        NAME Luma.Smoke.OpenGL
        COMMAND $<TARGET_FILE:Luma> --smoke-test=120 --api=opengl
    )
    set_tests_properties(
        Luma.Smoke.OpenGL
        PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            LABELS "smoke;opengl"
    )
endif()
