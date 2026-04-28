# Define a named Zephyr library
zephyr_library_named(ws2812_common)

# Module source
zephyr_library_sources(
    src/ws2812_common.c
)

# Make headers visible to any target that links this library
target_include_directories(ws2812_common PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)
