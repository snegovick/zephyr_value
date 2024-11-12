if(CONFIG_VALUE_PARAMS)
  zephyr_library()

  zephyr_library_sources(params.c)
  zephyr_library_sources_ifdef(CONFIG_VALUE_PARAMS_SHELL params_shell.c)
endif()
