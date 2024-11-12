if(CONFIG_VALUE_CALC)
  zephyr_library()

  zephyr_library_sources(calc.c)
  zephyr_library_sources_ifdef(CONFIG_VALUE_CALC_SHELL calc_shell.c)
endif()
