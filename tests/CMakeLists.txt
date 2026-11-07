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

add_executable(VantaEngineModelImportTests
    model_import_tests.cpp
    ../engine/src/assets/ObjLoader.cpp
)
vanta_configure_test(VantaEngineModelImportTests)
add_test(NAME VantaEngine.ModelImportTests COMMAND VantaEngineModelImportTests)

add_executable(VantaEngineProceduralWorldTests
    procedural_world_tests.cpp
    ../engine/src/world/ProceduralWorld.cpp
)
vanta_configure_test(VantaEngineProceduralWorldTests)
add_test(NAME VantaEngine.ProceduralWorldTests COMMAND VantaEngineProceduralWorldTests)

add_executable(VantaEngineGltfImportTests
    gltf_import_tests.cpp
    ../engine/src/assets/GltfLoader.cpp
)
vanta_configure_test(VantaEngineGltfImportTests)
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

add_executable(VantaEnginePhysicsTests
    physics_tests.cpp
    ../engine/src/physics/PhysicsWorld.cpp
)
vanta_configure_test(VantaEnginePhysicsTests)
add_test(NAME VantaEngine.PhysicsTests COMMAND VantaEnginePhysicsTests)

add_executable(VantaEngineSceneTests
    scene_tests.cpp
    ../engine/src/core/Scene.cpp
)
vanta_configure_test(VantaEngineSceneTests)
add_test(NAME VantaEngine.SceneTests COMMAND VantaEngineSceneTests)

add_executable(VantaEngineAssetStoreTests
    asset_store_tests.cpp
    ../engine/src/assets/ObjLoader.cpp
    ../engine/src/assets/GltfLoader.cpp
    ../engine/src/assets/ModelAssetStore.cpp
)
vanta_configure_test(VantaEngineAssetStoreTests)
add_test(NAME VantaEngine.AssetStoreTests COMMAND VantaEngineAssetStoreTests)

add_executable(VantaEnginePhysicsEventTests
    physics_event_tests.cpp
    ../engine/src/physics/PhysicsEventTracker.cpp
)
vanta_configure_test(VantaEnginePhysicsEventTests)
add_test(NAME VantaEngine.PhysicsEventTests COMMAND VantaEnginePhysicsEventTests)

add_executable(VantaEngineInputActionTests
    input_action_tests.cpp
    ../engine/src/platform/InputActionMap.cpp
)
vanta_configure_test(VantaEngineInputActionTests)
add_test(NAME VantaEngine.InputActionTests COMMAND VantaEngineInputActionTests)

add_executable(VantaEngineSceneSubmissionTests
    scene_submission_tests.cpp
    ../engine/src/renderer/SceneSubmission.cpp
    ../engine/src/renderer/SceneGeometry.cpp
)
vanta_configure_test(VantaEngineSceneSubmissionTests)
add_test(NAME VantaEngine.SceneSubmissionTests COMMAND VantaEngineSceneSubmissionTests)

add_executable(VantaEngineTextureProcessingTests
    texture_processing_tests.cpp
    ../engine/src/assets/TextureProcessing.cpp
)
target_compile_definitions(VantaEngineTextureProcessingTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
vanta_configure_test(VantaEngineTextureProcessingTests)
add_test(NAME VantaEngine.TextureProcessingTests COMMAND VantaEngineTextureProcessingTests)

add_executable(VantaEngineShaderValidationTests
    renderer_shader_validation_tests.cpp
)
vanta_configure_test(VantaEngineShaderValidationTests)
target_link_libraries(VantaEngineShaderValidationTests PRIVATE VantaEngine::Runtime)
add_test(NAME VantaEngine.ShaderValidationTests COMMAND VantaEngineShaderValidationTests)
