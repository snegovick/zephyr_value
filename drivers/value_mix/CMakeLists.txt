if(CONFIG_VALUE_MIX)
  zephyr_library()

  zephyr_library_sources(mix.c)
  zephyr_library_sources_ifdef(CONFIG_VALUE_MIX_SHELL mix_shell.c)
endif()
