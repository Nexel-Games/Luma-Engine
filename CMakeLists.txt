cmake_minimum_required(VERSION 3.21)

project(Luma LANGUAGES C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LUMA_ENABLE_PHYSX "Enable native PhysX backend integration" OFF)
option(LUMA_ENABLE_BLAST "Enable Blast destruction runtime integration" OFF)

set(LUMA_BLAST_RUNTIME_ROOT "${CMAKE_SOURCE_DIR}/thirdparty/Blast" CACHE PATH "Root directory containing vendored Blast runtime lib/bin folders")

function(luma_require_file dependency_path dependency_label)
    if(NOT EXISTS "${dependency_path}")
        message(FATAL_ERROR "${dependency_label} is missing: ${dependency_path}")
    endif()
endfunction()

file(GLOB LUA_RUNTIME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/thirdparty/lua-5.5.0/src/*.c")
list(FILTER LUA_RUNTIME_SOURCES EXCLUDE REGEX ".*/lua\\.c$")
list(FILTER LUA_RUNTIME_SOURCES EXCLUDE REGEX ".*/luac\\.c$")

set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/assimp EXCLUDE_FROM_ALL)

if(LUMA_ENABLE_PHYSX)
    set(PHYSX_ROOT_DIR "${CMAKE_SOURCE_DIR}/thirdparty/PhysX/physx" CACHE PATH "" FORCE)
    list(APPEND CMAKE_MODULE_PATH "${PHYSX_ROOT_DIR}/source/compiler/cmake/modules")
    set(TARGET_BUILD_PLATFORM "windows" CACHE STRING "" FORCE)
    set(PX_OUTPUT_LIB_DIR "${CMAKE_BINARY_DIR}/thirdparty/physx/lib" CACHE PATH "" FORCE)
    set(PX_OUTPUT_BIN_DIR "${CMAKE_BINARY_DIR}/thirdparty/physx/bin" CACHE PATH "" FORCE)
    set(PHYSX_FREEGLUT_STUB_DIR "${CMAKE_BINARY_DIR}/thirdparty/physx/freeglut/bin/win64")
    file(MAKE_DIRECTORY "${PHYSX_FREEGLUT_STUB_DIR}")
    file(WRITE "${PHYSX_FREEGLUT_STUB_DIR}/freeglut.dll" "")
    file(WRITE "${PHYSX_FREEGLUT_STUB_DIR}/freeglutd.dll" "")
    set(PHYSX_SLN_FREEGLUT_PATH "${CMAKE_BINARY_DIR}/thirdparty/physx/freeglut/bin" CACHE PATH "" FORCE)
    set(PUBLIC_RELEASE 1 CACHE BOOL "" FORCE)
    set(PX_GENERATE_STATIC_LIBRARIES ON CACHE BOOL "" FORCE)
    set(PX_BUILDSNIPPETS OFF CACHE BOOL "" FORCE)
    set(PX_BUILDPVDRUNTIME OFF CACHE BOOL "" FORCE)
    set(PX_GENERATE_GPU_PROJECTS OFF CACHE BOOL "" FORCE)
    set(PX_GENERATE_GPU_PROJECTS_ONLY OFF CACHE BOOL "" FORCE)
    set(PX_FLOAT_POINT_PRECISE_MATH OFF CACHE BOOL "" FORCE)
    set(NV_USE_STATIC_WINCRT OFF CACHE BOOL "" FORCE)
    set(NV_USE_DEBUG_WINCRT ON CACHE BOOL "" FORCE)
    add_subdirectory(thirdparty/PhysX/physx/source/compiler/cmake EXCLUDE_FROM_ALL)
endif()

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
    engine/runtime/Audio/Core/AudioSystem.cpp
    engine/runtime/Scripting/ScriptEngine.cpp
    engine/runtime/Scripting/LuaScriptRuntime.cpp
    engine/runtime/Scripting/Lua/LuaBindings.cpp
    engine/runtime/Scripting/Lua/LuaHelpers.cpp
    engine/runtime/Scripting/Lua/LuaState.cpp
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
     src/Editor/Content/ContentRootWatchService.cpp
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
     src/Editor/Panels/Inspector/InspectorAudioPanel.cpp
     src/Editor/Panels/Inspector/InspectorCameraLightingPanel.cpp
     src/Editor/Panels/Inspector/InspectorDestructionPanel.cpp
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
     src/Editor/Panels/Inspector/InspectorScriptPanel.cpp
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
    engine/runtime/Physics/Destruction/BlastDestructionService.cpp
    engine/runtime/Scene/VehicleInputSystem.cpp
    engine/runtime/Renderer/PrimitiveMeshFactory.cpp
    src/Layers/EditorLayer.cpp
    src/Layers/ProjectManagerLayer.cpp
    src/Layers/TriangleLayer.cpp
    engine/runtime/Scene/Scene.cpp
    engine/runtime/Scene/PrefabSerializer.cpp
    engine/runtime/Scene/SceneSerializer.cpp
    engine/runtime/Scene/VehicleTuningAsset.cpp
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
    thirdparty/miniaudio/miniaudio.c
    ${LUA_RUNTIME_SOURCES}
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
        ${CMAKE_SOURCE_DIR}/thirdparty/PhysX/physx/include/common
        ${CMAKE_SOURCE_DIR}/thirdparty/PhysX/physx/include/geometry
        ${CMAKE_SOURCE_DIR}/thirdparty/PhysX/physx/include/foundation
        ${CMAKE_SOURCE_DIR}/thirdparty/PhysX/physx/include/extensions
        ${CMAKE_SOURCE_DIR}/thirdparty/PhysX/physx/include/characterkinematic
        ${CMAKE_SOURCE_DIR}/thirdparty/Blast/sdk/lowlevel/include
        ${CMAKE_SOURCE_DIR}/thirdparty/Blast/sdk/globals/include
        ${CMAKE_SOURCE_DIR}/thirdparty/Blast/sdk/toolkit/include
        ${CMAKE_SOURCE_DIR}/thirdparty/Blast/sdk/extensions/physx/include
        ${CMAKE_SOURCE_DIR}/thirdparty/Blast/sdk/extensions/shaders/include
        ${CMAKE_SOURCE_DIR}/thirdparty/Blast/sdk/extensions/stress/include
        ${CMAKE_SOURCE_DIR}/thirdparty/Blast/shared/filebuf/include
        ${CMAKE_SOURCE_DIR}/thirdparty/imgui
        ${CMAKE_SOURCE_DIR}/thirdparty/imgui/backends
        ${CMAKE_SOURCE_DIR}/thirdparty/ImGuizmo
        ${CMAKE_SOURCE_DIR}/thirdparty/lua-5.5.0/src
        ${CMAKE_SOURCE_DIR}/thirdparty
        ${CMAKE_SOURCE_DIR}/thirdparty/stb/include
        ${CMAKE_SOURCE_DIR}/thirdparty/assimp/contrib/zip/src
        ${CMAKE_SOURCE_DIR}/thirdparty/entt/src
)

set(GLFW_LIB_DEBUG "${CMAKE_SOURCE_DIR}/thirdparty/glfw/debug/lib/glfw3dll.lib")
set(GLFW_LIB_RELEASE "${CMAKE_SOURCE_DIR}/thirdparty/glfw/lib/glfw3dll.lib")
set(GLAD_LIB_DEBUG "${CMAKE_SOURCE_DIR}/thirdparty/glad/debug/lib/glad.lib")
set(GLAD_LIB_RELEASE "${CMAKE_SOURCE_DIR}/thirdparty/glad/lib/glad.lib")
set(GLFW_DLL_DEBUG "${CMAKE_SOURCE_DIR}/thirdparty/glfw/debug/bin/glfw3.dll")
set(GLFW_DLL_RELEASE "${CMAKE_SOURCE_DIR}/thirdparty/glfw/bin/glfw3.dll")

luma_require_file("${GLFW_LIB_DEBUG}" "Vendored GLFW debug import library")
luma_require_file("${GLFW_LIB_RELEASE}" "Vendored GLFW release import library")
luma_require_file("${GLAD_LIB_DEBUG}" "Vendored GLAD debug library")
luma_require_file("${GLAD_LIB_RELEASE}" "Vendored GLAD release library")
luma_require_file("${GLFW_DLL_DEBUG}" "Vendored GLFW debug runtime DLL")
luma_require_file("${GLFW_DLL_RELEASE}" "Vendored GLFW release runtime DLL")

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

if(WIN32)
    target_link_libraries(
        Luma
        PRIVATE
            winmm
            ole32
            uuid
            avrt
    )
endif()

if(LUMA_ENABLE_PHYSX)
    target_link_libraries(
        Luma
        PRIVATE
            PhysXFoundation
            PhysXCommon
            PhysX
            PhysXCharacterKinematic
            PhysXCooking
            PhysXExtensions
    )
endif()

if(LUMA_ENABLE_BLAST)
    set(BLAST_RUNTIME_LIB_DIR "${LUMA_BLAST_RUNTIME_ROOT}/lib/vs18win64-runtime")
    set(BLAST_RUNTIME_BIN_DIR "${LUMA_BLAST_RUNTIME_ROOT}/bin/vs18win64-runtime")

    foreach(_cfg debug release)
        foreach(_lib NvBlast_x64 NvBlastGlobals_x64 NvBlastTk_x64 NvBlastExtShaders_x64 NvBlastExtStress_x64)
            luma_require_file(
                "${BLAST_RUNTIME_LIB_DIR}/${_cfg}/${_lib}.lib"
                "Vendored Blast runtime library"
            )
        endforeach()
        foreach(_dll NvBlast_x64 NvBlastGlobals_x64 NvBlastTk_x64 NvBlastExtShaders_x64 NvBlastExtStress_x64)
            luma_require_file(
                "${BLAST_RUNTIME_BIN_DIR}/${_cfg}/${_dll}.dll"
                "Vendored Blast runtime DLL"
            )
        endforeach()
    endforeach()

    target_link_libraries(
        Luma
        PRIVATE
            debug "${BLAST_RUNTIME_LIB_DIR}/debug/NvBlast_x64.lib"
            optimized "${BLAST_RUNTIME_LIB_DIR}/release/NvBlast_x64.lib"
            debug "${BLAST_RUNTIME_LIB_DIR}/debug/NvBlastGlobals_x64.lib"
            optimized "${BLAST_RUNTIME_LIB_DIR}/release/NvBlastGlobals_x64.lib"
            debug "${BLAST_RUNTIME_LIB_DIR}/debug/NvBlastTk_x64.lib"
            optimized "${BLAST_RUNTIME_LIB_DIR}/release/NvBlastTk_x64.lib"
            debug "${BLAST_RUNTIME_LIB_DIR}/debug/NvBlastExtShaders_x64.lib"
            optimized "${BLAST_RUNTIME_LIB_DIR}/release/NvBlastExtShaders_x64.lib"
            debug "${BLAST_RUNTIME_LIB_DIR}/debug/NvBlastExtStress_x64.lib"
            optimized "${BLAST_RUNTIME_LIB_DIR}/release/NvBlastExtStress_x64.lib"
    )

    foreach(_blast_dll NvBlast_x64 NvBlastGlobals_x64 NvBlastTk_x64 NvBlastExtShaders_x64 NvBlastExtStress_x64)
        add_custom_command(
            TARGET Luma POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<IF:$<CONFIG:Debug>,${BLAST_RUNTIME_BIN_DIR}/debug/${_blast_dll}.dll,${BLAST_RUNTIME_BIN_DIR}/release/${_blast_dll}.dll>"
                    "$<TARGET_FILE_DIR:Luma>/${_blast_dll}.dll"
        )
    endforeach()
endif()

target_compile_definitions(
    Luma
    PRIVATE
        IMGUI_IMPL_OPENGL_LOADER_GLAD
        LUMA_ENABLE_PHYSX=$<BOOL:${LUMA_ENABLE_PHYSX}>
        LUMA_ENABLE_BLAST=$<BOOL:${LUMA_ENABLE_BLAST}>
)

if(LUMA_ENABLE_PHYSX)
    target_compile_definitions(Luma PRIVATE PX_PHYSX_STATIC_LIB)
endif()

if(MSVC)
    target_compile_options(Luma PRIVATE /FS)
endif()

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
    add_test(
        NAME Luma.Smoke.LuaRuntime
        COMMAND $<TARGET_FILE:Luma> --lua-runtime-smoke-test
    )
    set_tests_properties(
        Luma.Smoke.LuaRuntime
        PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            LABELS "smoke;lua;scripting"
    )
    add_test(
        NAME Luma.Smoke.AudioRuntime
        COMMAND $<TARGET_FILE:Luma> --audio-runtime-smoke-test
    )
    set_tests_properties(
        Luma.Smoke.AudioRuntime
        PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            LABELS "smoke;audio"
    )
    add_test(
        NAME Luma.Smoke.SceneRuntime
        COMMAND $<TARGET_FILE:Luma> --scene-runtime-smoke-test
    )
    set_tests_properties(
        Luma.Smoke.SceneRuntime
        PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            LABELS "smoke;scene;serialization"
    )
    add_test(
        NAME Luma.Smoke.PrefabRuntime
        COMMAND $<TARGET_FILE:Luma> --prefab-runtime-smoke-test
    )
    set_tests_properties(
        Luma.Smoke.PrefabRuntime
        PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            LABELS "smoke;prefab;serialization"
    )
endif()
