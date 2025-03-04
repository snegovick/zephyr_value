if(CONFIG_REGULATOR_EXTENDED)
  zephyr_library()

  zephyr_library_sources(regulator_extended.c)

  zephyr_library_sources_ifdef(CONFIG_REGULATOR_EXTENDED_SHELL
    regulator_extended_shell.c
  )
endif()
