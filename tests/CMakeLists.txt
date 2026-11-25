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

add_executable(VantaEngineModelImportTests model_import_tests.cpp)
vanta_configure_test(VantaEngineModelImportTests)
target_link_libraries(VantaEngineModelImportTests PRIVATE VantaEngine::Core)
add_test(NAME VantaEngine.ModelImportTests COMMAND VantaEngineModelImportTests)
if(EXISTS "${PROJECT_SOURCE_DIR}/build/generated/starter_cabin.obj")
    add_test(NAME VantaEngine.GeneratedModelImportTests
        COMMAND VantaEngineModelImportTests "${PROJECT_SOURCE_DIR}/build/generated/starter_cabin.obj")
endif()

add_executable(VantaEngineProceduralWorldTests procedural_world_tests.cpp)
vanta_configure_test(VantaEngineProceduralWorldTests)
target_link_libraries(VantaEngineProceduralWorldTests PRIVATE VantaEngine::Core)
add_test(NAME VantaEngine.ProceduralWorldTests COMMAND VantaEngineProceduralWorldTests)

add_executable(VantaEngineGltfImportTests gltf_import_tests.cpp)
vanta_configure_test(VantaEngineGltfImportTests)
target_link_libraries(VantaEngineGltfImportTests PRIVATE VantaEngine::Core)
add_test(NAME VantaEngine.GltfImportTests COMMAND VantaEngineGltfImportTests)

add_executable(VantaEngineAdaptiveBudgetTests
    adaptive_world_budget_tests.cpp
)
vanta_configure_test(VantaEngineAdaptiveBudgetTests)
add_test(NAME VantaEngine.AdaptiveWorldBudgetTests COMMAND VantaEngineAdaptiveBudgetTests)

add_executable(VantaEngineCameraTests
    camera_tests.cpp
)
vanta_configure_test(VantaEngineCameraTests)
add_test(NAME VantaEngine.CameraTests COMMAND VantaEngineCameraTests)

add_executable(VantaEngineFixedStepTests
    fixed_step_tests.cpp
)
vanta_configure_test(VantaEngineFixedStepTests)
add_test(NAME VantaEngine.FixedStepTests COMMAND VantaEngineFixedStepTests)

add_executable(VantaEngineInputTests
    input_tests.cpp
)
vanta_configure_test(VantaEngineInputTests)
add_test(NAME VantaEngine.InputTests COMMAND VantaEngineInputTests)

add_executable(VantaEnginePhysicsTests physics_tests.cpp)
vanta_configure_test(VantaEnginePhysicsTests)
target_link_libraries(VantaEnginePhysicsTests PRIVATE VantaEngine::Core)
add_test(NAME VantaEngine.PhysicsTests COMMAND VantaEnginePhysicsTests)

add_executable(VantaEngineSceneTests scene_tests.cpp)
vanta_configure_test(VantaEngineSceneTests)
target_link_libraries(VantaEngineSceneTests PRIVATE VantaEngine::Core)
add_test(NAME VantaEngine.SceneTests COMMAND VantaEngineSceneTests)

add_executable(VantaEngineAssetStoreTests asset_store_tests.cpp)
vanta_configure_test(VantaEngineAssetStoreTests)
target_link_libraries(VantaEngineAssetStoreTests PRIVATE VantaEngine::Core)
add_test(NAME VantaEngine.AssetStoreTests COMMAND VantaEngineAssetStoreTests)

add_executable(VantaEnginePhysicsEventTests physics_event_tests.cpp)
vanta_configure_test(VantaEnginePhysicsEventTests)
target_link_libraries(VantaEnginePhysicsEventTests PRIVATE VantaEngine::Core)
add_test(NAME VantaEngine.PhysicsEventTests COMMAND VantaEnginePhysicsEventTests)

add_executable(VantaEngineInputActionTests input_action_tests.cpp)
vanta_configure_test(VantaEngineInputActionTests)
target_link_libraries(VantaEngineInputActionTests PRIVATE VantaEngine::Core)
add_test(NAME VantaEngine.InputActionTests COMMAND VantaEngineInputActionTests)

add_executable(VantaEngineSceneSubmissionTests
    scene_submission_tests.cpp
    ../engine/src/renderer/SceneSubmission.cpp
    ../engine/src/renderer/SceneGeometry.cpp
    ../engine/src/renderer/MeshletBuilder.cpp
    ../engine/src/renderer/MeshletHierarchy.cpp
    ../engine/src/renderer/EnvironmentLighting.cpp
)
vanta_configure_test(VantaEngineSceneSubmissionTests)
add_test(NAME VantaEngine.SceneSubmissionTests COMMAND VantaEngineSceneSubmissionTests)

add_executable(VantaEngineTextureProcessingTests texture_processing_tests.cpp)
vanta_configure_test(VantaEngineTextureProcessingTests)
target_link_libraries(VantaEngineTextureProcessingTests PRIVATE VantaEngine::AssetPipeline)
add_test(NAME VantaEngine.TextureProcessingTests COMMAND VantaEngineTextureProcessingTests)

add_executable(VantaEngineShaderValidationTests
    renderer_shader_validation_tests.cpp
)
vanta_configure_test(VantaEngineShaderValidationTests)
target_compile_definitions(VantaEngineShaderValidationTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(VantaEngineShaderValidationTests PRIVATE VantaEngine::Runtime)
add_test(NAME VantaEngine.ShaderValidationTests COMMAND VantaEngineShaderValidationTests)

add_executable(VantaEngineHdrEnvironmentTests
    hdr_environment_tests.cpp
    ../engine/src/renderer/HdrEnvironment.cpp
    ../engine/src/renderer/EnvironmentLighting.cpp
)
vanta_configure_test(VantaEngineHdrEnvironmentTests)
add_test(NAME VantaEngine.HdrEnvironmentTests COMMAND VantaEngineHdrEnvironmentTests)
