# nice_view_rotten: custom nice!view screens (bongo cat on the left half,
# spinning cube on the right half). Shield files are in
# boards/shields/nice_view_rotten, the code in src/nice_view_rotten.
if(CONFIG_ZMK_DISPLAY AND CONFIG_NICE_VIEW_ROTTEN_WIDGET)
  zephyr_library()
  zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
  zephyr_library_sources(
    src/nice_view_rotten/custom_status_screen.c
    src/nice_view_rotten/util.c
    src/nice_view_rotten/bolt.c
    src/nice_view_rotten/sprites.c
  )
  if(NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    zephyr_library_sources(src/nice_view_rotten/status.c)
  else()
    zephyr_library_sources(src/nice_view_rotten/peripheral_status.c)
  endif()
endif()
