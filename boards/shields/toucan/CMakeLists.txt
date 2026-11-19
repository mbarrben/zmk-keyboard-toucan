if(CONFIG_SHIELD_TOUCAN_DONGLE AND CONFIG_ZMK_HID_INDICATORS)
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
  zephyr_library_sources(tofu65_indicator_relay.c)
endif()
