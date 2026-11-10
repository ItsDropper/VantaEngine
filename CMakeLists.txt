cmake_minimum_required(VERSION 3.25)

project(VantaEngine
    VERSION 0.2.0
    DESCRIPTION "A lightweight native 3D game engine"
    LANGUAGES CXX
)

include(CTest)

option(VANTA_ENABLE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(VANTA_ENABLE_STREAMLINE "Enable the NVIDIA Streamline SDK integration" OFF)
set(VANTA_STREAMLINE_SDK_ROOT "" CACHE PATH "Path to the unpacked NVIDIA Streamline SDK")

if(VANTA_ENABLE_STREAMLINE)
    if(NOT WIN32)
        message(FATAL_ERROR "NVIDIA Streamline integration currently requires the Windows D3D12 backend.")
    endif()
    if(NOT EXISTS "${VANTA_STREAMLINE_SDK_ROOT}/CMakeLists.txt")
        message(FATAL_ERROR "VANTA_ENABLE_STREAMLINE requires VANTA_STREAMLINE_SDK_ROOT to point to the unpacked Streamline SDK.")
    endif()

    # Streamline's import library replaces the normal DXGI/D3D12 import libraries.
    # Its CMake project also copies the requested, official runtime plugins beside
    # the engine executable.
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set(STREAMLINE_FEATURE_DLSS_SR ON CACHE BOOL "Include DLSS Super Resolution" FORCE)
    set(STREAMLINE_FEATURE_DLSS_RR ON CACHE BOOL "Include DLSS Ray Reconstruction" FORCE)
    set(STREAMLINE_FEATURE_DLSS_FG ON CACHE BOOL "Include DLSS Frame Generation" FORCE)
    set(STREAMLINE_FEATURE_REFLEX ON CACHE BOOL "Include NVIDIA Reflex" FORCE)
    set(STREAMLINE_FEATURE_NIS ON CACHE BOOL "Include NVIDIA Image Scaling" FORCE)
    set(STREAMLINE_IMPORT_AS_INTERFACE OFF CACHE BOOL "Use Streamline import library" FORCE)
    add_subdirectory("${VANTA_STREAMLINE_SDK_ROOT}" "${CMAKE_BINARY_DIR}/streamline")
endif()

add_subdirectory(engine)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
