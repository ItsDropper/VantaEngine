set(VANTA_RUNTIME_SOURCES
    src/core/Engine.cpp
    src/platform/Window.cpp
    src/renderer/D3D11Renderer.cpp
    src/renderer/D3D11Resources.cpp
    src/renderer/D3D12Renderer.cpp
    src/renderer/D3D12Resources.cpp
    src/renderer/D3D12Pipelines.cpp
    src/renderer/ProceduralWorld.cpp
    src/renderer/NvidiaRendering.cpp
    src/renderer/ObjLoader.cpp
    src/renderer/GltfLoader.cpp
    src/renderer/WicTextureLoader.cpp
)

add_library(VantaEngineRuntime STATIC ${VANTA_RUNTIME_SOURCES})
add_library(VantaEngine::Runtime ALIAS VantaEngineRuntime)

target_compile_features(VantaEngineRuntime PUBLIC cxx_std_23)

target_include_directories(VantaEngineRuntime
    PUBLIC include
)

get_filename_component(VANTA_ENGINE_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../assets" ABSOLUTE)

target_compile_definitions(VantaEngineRuntime PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    VANTA_ASSET_DIR="${VANTA_ENGINE_ASSET_DIR}"
)

target_link_libraries(VantaEngineRuntime PUBLIC
    d3d11
    d3d12
    dxgi
    d3dcompiler
    windowscodecs
    ole32
    user32
)

if(MSVC)
    target_compile_options(VantaEngineRuntime PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /utf-8
        $<$<BOOL:${VANTA_ENABLE_WARNINGS_AS_ERRORS}>:/WX>
    )
else()
    target_compile_options(VantaEngineRuntime PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        $<$<BOOL:${VANTA_ENABLE_WARNINGS_AS_ERRORS}>:-Werror>
    )
endif()

add_executable(VantaEngine src/main.cpp)
target_compile_features(VantaEngine PRIVATE cxx_std_23)
target_link_libraries(VantaEngine PRIVATE VantaEngine::Runtime)

set_target_properties(VantaEngine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(BUILD_TESTING)
    add_test(
        NAME VantaEngine.Startup
        COMMAND VantaEngine --self-test
    )
endif()
