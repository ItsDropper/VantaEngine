# Engine Core contains deterministic, CPU-side runtime systems. It has no Win32 or
# graphics-API link dependencies, so it can be tested and reused independently.
set(VANTA_CORE_SOURCES
    src/core/Scene.cpp
    src/physics/PhysicsWorld.cpp
    src/physics/PhysicsEventTracker.cpp
    src/platform/InputActionMap.cpp
    src/world/ProceduralWorld.cpp
    src/assets/ObjLoader.cpp
    src/assets/ModelAssetStore.cpp
    src/assets/GltfLoader.cpp
)

add_library(VantaEngineCore STATIC ${VANTA_CORE_SOURCES})
add_library(VantaEngine::Core ALIAS VantaEngineCore)
target_compile_features(VantaEngineCore PUBLIC cxx_std_23)
target_include_directories(VantaEngineCore PUBLIC include)

# CPU asset transforms and Windows-backed asset I/O are separate from both the
# platform-neutral Core and the graphics renderer.
add_library(VantaEngineAssetPipeline STATIC
    src/assets/TextureProcessing.cpp
    src/assets/WicTextureLoader.cpp
)
add_library(VantaEngine::AssetPipeline ALIAS VantaEngineAssetPipeline)
target_compile_features(VantaEngineAssetPipeline PUBLIC cxx_std_23)
target_include_directories(VantaEngineAssetPipeline PUBLIC include)
target_compile_definitions(VantaEngineAssetPipeline PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
)
target_link_libraries(VantaEngineAssetPipeline PUBLIC
    VantaEngine::Core windowscodecs ole32
)

# GPU/renderer owns graphics backends, GPU resource management, renderer-side
# scene submission, mesh preprocessing, and platform image decoding.
set(VANTA_RENDERER_SOURCES
    src/renderer/D3D11Renderer.cpp
    src/renderer/D3D11Resources.cpp
    src/renderer/D3D12Renderer.cpp
    src/renderer/D3D12Resources.cpp
    src/renderer/D3D12Pipelines.cpp
    src/renderer/EnvironmentLighting.cpp
    src/renderer/SceneSubmission.cpp
    src/renderer/SceneGeometry.cpp
    src/renderer/MeshletBuilder.cpp
    src/renderer/MeshletHierarchy.cpp
    src/renderer/NvidiaRendering.cpp
    src/renderer/NvidiaStreamline.cpp
    src/renderer/NvidiaAftermath.cpp
    src/renderer/D3D12RaytracingScene.cpp
)

add_library(VantaEngineRenderer STATIC ${VANTA_RENDERER_SOURCES})
add_library(VantaEngine::Renderer ALIAS VantaEngineRenderer)
target_compile_features(VantaEngineRenderer PUBLIC cxx_std_23)
target_include_directories(VantaEngineRenderer PUBLIC include)
target_link_libraries(VantaEngineRenderer PUBLIC VantaEngine::Core VantaEngine::AssetPipeline
    d3d11 d3d12 dxgi d3dcompiler)

get_filename_component(VANTA_ENGINE_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../assets" ABSOLUTE)
target_compile_definitions(VantaEngineRenderer PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
    VANTA_ASSET_DIR="${VANTA_ENGINE_ASSET_DIR}"
)

if(VANTA_ENABLE_STREAMLINE)
    target_link_libraries(VantaEngineRenderer PUBLIC streamline wintrust)
    target_compile_definitions(VantaEngineRenderer PRIVATE VANTA_ENABLE_STREAMLINE=1)
endif()

if(VANTA_ENABLE_AFTERMATH)
    target_include_directories(VantaEngineRenderer PRIVATE "${VANTA_AFTERMATH_SDK_ROOT}/include")
    target_link_libraries(VantaEngineRenderer PUBLIC "${VANTA_AFTERMATH_LIBRARY}")
    target_compile_definitions(VantaEngineRenderer PRIVATE VANTA_ENABLE_AFTERMATH=1)
endif()

# Runtime is the platform composition root: it owns the main loop and Win32
# window, and connects Core to a selected renderer.
add_library(VantaEngineRuntime STATIC
    src/core/Engine.cpp
    src/platform/Window.cpp
)
add_library(VantaEngine::Runtime ALIAS VantaEngineRuntime)
target_compile_features(VantaEngineRuntime PUBLIC cxx_std_23)
target_include_directories(VantaEngineRuntime PUBLIC include)
target_compile_definitions(VantaEngineRuntime PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
)
target_link_libraries(VantaEngineRuntime PUBLIC
    VantaEngine::Core VantaEngine::Renderer user32
)

function(vanta_configure_engine_target target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE
            /W4 /permissive- /Zc:__cplusplus /utf-8
            $<$<BOOL:${VANTA_ENABLE_WARNINGS_AS_ERRORS}>:/WX>
        )
    else()
        target_compile_options(${target_name} PRIVATE
            -Wall -Wextra -Wpedantic
            $<$<BOOL:${VANTA_ENABLE_WARNINGS_AS_ERRORS}>:-Werror>
        )
    endif()
endfunction()

vanta_configure_engine_target(VantaEngineCore)
vanta_configure_engine_target(VantaEngineAssetPipeline)
vanta_configure_engine_target(VantaEngineRenderer)
vanta_configure_engine_target(VantaEngineRuntime)

add_executable(VantaEngine src/main.cpp)
target_compile_features(VantaEngine PRIVATE cxx_std_23)
target_link_libraries(VantaEngine PRIVATE VantaEngine::Runtime)
set_target_properties(VantaEngine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(VANTA_ENABLE_AFTERMATH)
    add_custom_command(TARGET VantaEngine POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${VANTA_AFTERMATH_RUNTIME}" "$<TARGET_FILE_DIR:VantaEngine>"
        VERBATIM)
endif()

if(BUILD_TESTING)
    add_test(NAME VantaEngine.Startup COMMAND VantaEngine --self-test)
endif()
