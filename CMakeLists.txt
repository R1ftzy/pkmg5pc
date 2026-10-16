cmake_minimum_required(VERSION 3.24)

project(pkmng5pc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/CompilerWarnings.cmake)

option(PKMNG5PC_BUILD_TESTS "Build unit tests" ON)
option(PKMNG5PC_BUILD_TOOLS "Build local extraction tools" ON)
option(PKMNG5PC_BUILD_RUNNER
    "Build the ndsrecomp runner and its SDL3 dependency (needs generated title banks)" OFF)

add_subdirectory(rom)

if(PKMNG5PC_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(PKMNG5PC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(PKMNG5PC_BUILD_RUNNER)
    include(cmake/Runner.cmake)
endif()
