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
    ${CMAKE_CURRENT_LIST_DIR}/modules/ws2812_common
  )
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(mchp_6dof_imu)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)

target_sources(app PRIVATE
    src/main.c
    src/bubble_level.c
    src/imu_chart.c
    src/imu_text.c
)

if (CONFIG_USE_IMG_UTIL)
  target_sources(app PRIVATE src/img_util.c)
endif()

if (CONFIG_WS2812_COMMON)
  target_link_libraries(app PRIVATE ws2812_common)
endif()
