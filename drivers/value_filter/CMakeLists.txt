if(CONFIG_VALUE_FILTER)
  zephyr_library()

  zephyr_library_sources(filter.c)

  zephyr_library_sources_ifdef(CONFIG_VALUE_FILTER_SHELL
    filter_shell.c
  )
endif()
