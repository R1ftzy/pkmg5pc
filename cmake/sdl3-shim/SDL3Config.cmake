# Shim for ndsrecomp's `find_package(SDL3 CONFIG REQUIRED)`.
#
# Our superbuild fetches SDL3 with FetchContent, which defines the SDL3::SDL3
# target in-tree but leaves no installed config package for find_package() to
# locate. Pointing SDL3_DIR at this directory satisfies the lookup. The guard
# below turns a wiring mistake into a clear error instead of a confusing
# "SDL3::SDL3 not found" much later.

if(NOT TARGET SDL3::SDL3)
    if(TARGET SDL3-shared)
        add_library(SDL3::SDL3 ALIAS SDL3-shared)
    elseif(TARGET SDL3-static)
        add_library(SDL3::SDL3 ALIAS SDL3-static)
    endif()
endif()

if(NOT TARGET SDL3::SDL3)
    message(FATAL_ERROR
        "SDL3Config shim: SDL3::SDL3 is not defined. SDL3 must be fetched with "
        "FetchContent before ndsrecomp's runner is added.")
endif()

set(SDL3_FOUND TRUE)
