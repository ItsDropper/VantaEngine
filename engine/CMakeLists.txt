add_executable(VantaEngine
    src/main.cpp
)

target_compile_features(VantaEngine PRIVATE cxx_std_23)

target_include_directories(VantaEngine
    PRIVATE include
)

if(MSVC)
    target_compile_options(VantaEngine PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /utf-8
    )
else()
    target_compile_options(VantaEngine PRIVATE
        -Wall
        -Wextra
        -Wpedantic
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
