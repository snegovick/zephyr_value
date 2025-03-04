if(CONFIG_CONDITION_MONITOR)
  zephyr_library()

  zephyr_library_sources(monitor.c)
endif()
