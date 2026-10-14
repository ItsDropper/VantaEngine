cmake_minimum_required(VERSION 3.25)

project(VantaEngine
    VERSION 0.2.0
    DESCRIPTION "A lightweight native 3D game engine"
    LANGUAGES CXX
)

include(CTest)

option(VANTA_ENABLE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

add_subdirectory(engine)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
