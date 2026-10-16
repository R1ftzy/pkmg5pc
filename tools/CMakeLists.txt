add_executable(pkmn_romdump
    romdump/main.cpp
)

target_link_libraries(pkmn_romdump PRIVATE pkmn::core pkmn_warnings)

add_executable(pkmn_bankgen
    bankgen/main.cpp
)

target_link_libraries(pkmn_bankgen PRIVATE pkmn::core pkmn_warnings)
