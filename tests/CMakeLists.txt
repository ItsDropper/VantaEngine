function(vanta_configure_test target_name)
    target_compile_features(${target_name} PRIVATE cxx_std_23)
    target_include_directories(${target_name} PRIVATE "${PROJECT_SOURCE_DIR}/engine/include")

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endfunction()

add_executable(VantaEngineTests
    engine_tests.cpp
)
vanta_configure_test(VantaEngineTests)
add_test(NAME VantaEngine.UnitTests COMMAND VantaEngineTests)

add_executable(VantaEngineMathTests
    math_tests.cpp
)
vanta_configure_test(VantaEngineMathTests)
add_test(NAME VantaEngine.MathTests COMMAND VantaEngineMathTests)
