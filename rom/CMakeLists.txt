add_library(pkmn_core STATIC
    src/compression.cpp
    src/extract.cpp
    src/narc.cpp
    src/nds.cpp
    src/sha1.cpp
    src/sha256.cpp
)

target_include_directories(pkmn_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(pkmn_core PRIVATE pkmn_warnings)

add_library(pkmn::core ALIAS pkmn_core)
