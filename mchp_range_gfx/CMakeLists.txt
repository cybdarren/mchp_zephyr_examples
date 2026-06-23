cmake_minimum_required(VERSION 3.20.0)

# ------------------------------------------------------------
# Board-specific DTS overlays (MUST be set before Zephyr)
# ------------------------------------------------------------
if(DEFINED BOARD)
  if(BOARD STREQUAL "same54_xpro")
    list(APPEND DTC_OVERLAY_FILE
      ${CMAKE_CURRENT_SOURCE_DIR}/boards/overlays/same54_xpro.overlay
    )
  elseif(BOARD STREQUAL "feather_m4_zephyr_ot")
    list(APPEND DTC_OVERLAY_FILE
      ${CMAKE_CURRENT_SOURCE_DIR}/boards/overlays/feather_m4_zephyr_ot.overlay
    )
  endif()
endif()

if(BOARD STREQUAL "feather_m4_zephyr_ot")
  list(APPEND ZEPHYR_EXTRA_MODULES
    ${CMAKE_CURRENT_LIST_DIR}/../mchp_common/modules/ws2812_common
  )
endif()

list(APPEND ZEPHYR_EXTRA_MODULES
  ${CMAKE_CURRENT_LIST_DIR}/../mchp_common/modules/mchp_serial
  ${CMAKE_CURRENT_LIST_DIR}/../mchp_common/modules/board_root
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(mchp_range_gfx)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_sources(app PRIVATE
    src/main.c
    src/range_text.c
    src/range_chart.c
    src/range_dial.c
    src/vl6180x.c
)

if (CONFIG_USE_WS2812)
  target_link_libraries(app PRIVATE ws2812_common)
endif()

if (CONFIG_USE_MCHP_SERIAL)
  target_link_libraries(app PRIVATE mchp_serial)
endif()
