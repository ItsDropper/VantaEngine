add_executable(VantaEngineTests
    engine_tests.cpp
    math_tests.cpp
)
target_compile_features(VantaEngineTests PRIVATE cxx_std_23)
target_include_directories(VantaEngineTests PRIVATE "${PROJECT_SOURCE_DIR}/engine/include")
if(MSVC)
    target_compile_options(VantaEngineTests PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
else()
    target_compile_options(VantaEngineTests PRIVATE -Wall -Wextra -Wpedantic)
endif()
set_target_properties(VantaEngineTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_test(NAME VantaEngine.UnitTests COMMAND VantaEngineTests)
