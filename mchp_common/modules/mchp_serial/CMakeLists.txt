zephyr_library_named(mchp_serial)

zephyr_library_sources(
    src/mchp_serial.c
)

target_include_directories(mchp_serial PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)
