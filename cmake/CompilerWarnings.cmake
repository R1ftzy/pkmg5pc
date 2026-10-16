add_library(pkmn_warnings INTERFACE)

if(MSVC)
    target_compile_options(pkmn_warnings INTERFACE
        /W4
        /permissive-
        /utf-8
    )
else()
    target_compile_options(pkmn_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
    )
endif()
