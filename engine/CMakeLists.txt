if(NOT WIN32)
    message(FATAL_ERROR "VantaEngine currently targets Windows. Cross-platform backends are planned.")
endif()

add_executable(VantaEngine
    src/main.cpp
    src/core/Engine.cpp
    src/platform/Window.cpp
    src/renderer/D3D11Renderer.cpp
    src/renderer/D3D12Renderer.cpp
    src/renderer/ProceduralWorld.cpp
    src/renderer/NvidiaRendering.cpp
    src/renderer/ObjLoader.cpp
    src/renderer/GltfLoader.cpp
    src/renderer/WicTextureLoader.cpp
)

target_compile_features(VantaEngine PRIVATE cxx_std_23)

target_include_directories(VantaEngine
    PRIVATE include
)

target_compile_definitions(VantaEngine PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    VANTA_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets"
)

target_link_libraries(VantaEngine PRIVATE
    d3d11
    d3d12
    dxgi
    d3dcompiler
    windowscodecs
    ole32
    user32
)

if(MSVC)
    target_compile_options(VantaEngine PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /utf-8
        $<$<BOOL:${VANTA_ENABLE_WARNINGS_AS_ERRORS}>:/WX>
    )
else()
    target_compile_options(VantaEngine PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        $<$<BOOL:${VANTA_ENABLE_WARNINGS_AS_ERRORS}>:-Werror>
    )
endif()

set_target_properties(VantaEngine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(BUILD_TESTING)
    add_test(
        NAME VantaEngine.Startup
        COMMAND VantaEngine --self-test
    )
endif()
