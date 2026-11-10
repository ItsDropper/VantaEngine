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
option(VANTA_ENABLE_AFTERMATH "Enable NVIDIA Nsight Aftermath GPU crash dumps" OFF)
set(VANTA_AFTERMATH_SDK_ROOT "" CACHE PATH "Path to the installed NVIDIA Nsight Aftermath SDK")

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
    if(CMAKE_GENERATOR_PLATFORM)
        set(STREAMLINE_TARGET_ARCH "${CMAKE_GENERATOR_PLATFORM}")
    else()
        set(STREAMLINE_TARGET_ARCH "x64")
    endif()
    set(_vanta_streamline_bin "${VANTA_STREAMLINE_SDK_ROOT}/bin/${STREAMLINE_TARGET_ARCH}")
    # NVIDIA's package CMake concatenates these directory variables directly
    # with DLL filenames, so preseed them with a trailing separator.
    if(EXISTS "${_vanta_streamline_bin}/sl.interposer.dll")
        set(STREAMLINE_PLUGIN_DLL_DIR "${_vanta_streamline_bin}/")
    elseif(EXISTS "${_vanta_streamline_bin}/development/sl.interposer.dll")
        set(STREAMLINE_PLUGIN_DLL_DIR "${_vanta_streamline_bin}/development/")
    endif()
    if(EXISTS "${_vanta_streamline_bin}/nvngx_dlss.dll")
        set(STREAMLINE_SUPPORT_DLL_DIR "${_vanta_streamline_bin}/")
    endif()
    set(STREAMLINE_FEATURE_DLSS_SR ON CACHE BOOL "Include DLSS Super Resolution" FORCE)
    set(STREAMLINE_FEATURE_DLSS_RR ON CACHE BOOL "Include DLSS Ray Reconstruction" FORCE)
    set(STREAMLINE_FEATURE_DLSS_FG ON CACHE BOOL "Include DLSS Frame Generation" FORCE)
    set(STREAMLINE_FEATURE_REFLEX ON CACHE BOOL "Include NVIDIA Reflex" FORCE)
    set(STREAMLINE_FEATURE_NIS ON CACHE BOOL "Include NVIDIA Image Scaling" FORCE)
    set(STREAMLINE_FEATURE_NVPERF ON CACHE BOOL "Include NVIDIA Nsight Perf plugin" FORCE)
    set(STREAMLINE_IMPORT_AS_INTERFACE ON CACHE BOOL "Use Streamline without import library" FORCE)
    add_subdirectory("${VANTA_STREAMLINE_SDK_ROOT}" "${CMAKE_BINARY_DIR}/streamline")
endif()

if(VANTA_ENABLE_AFTERMATH)
    if(NOT WIN32)
        message(FATAL_ERROR "Nsight Aftermath integration currently requires the Windows D3D12 backend.")
    endif()
    if(NOT EXISTS "${VANTA_AFTERMATH_SDK_ROOT}/include/GFSDK_Aftermath.h")
        message(FATAL_ERROR "VANTA_ENABLE_AFTERMATH requires VANTA_AFTERMATH_SDK_ROOT to point to the installed Nsight Aftermath SDK.")
    endif()
    if(CMAKE_GENERATOR_PLATFORM MATCHES "ARM64")
        set(_vanta_aftermath_arch arm64)
    else()
        set(_vanta_aftermath_arch x64)
    endif()
    set(_vanta_aftermath_lib "${VANTA_AFTERMATH_SDK_ROOT}/lib/${_vanta_aftermath_arch}")
    find_library(VANTA_AFTERMATH_LIBRARY
        NAMES GFSDK_AFTERMATH_Lib.${_vanta_aftermath_arch}
        PATHS "${_vanta_aftermath_lib}"
        NO_DEFAULT_PATH)
    if(NOT VANTA_AFTERMATH_LIBRARY)
        message(FATAL_ERROR "Nsight Aftermath SDK library not found in ${_vanta_aftermath_lib}.")
    endif()
    set(VANTA_AFTERMATH_RUNTIME "${_vanta_aftermath_lib}/GFSDK_AFTERMATH_Lib.${_vanta_aftermath_arch}.dll")
    if(NOT EXISTS "${VANTA_AFTERMATH_RUNTIME}")
        message(FATAL_ERROR "Nsight Aftermath runtime DLL not found: ${VANTA_AFTERMATH_RUNTIME}")
    endif()
endif()

add_subdirectory(engine)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
