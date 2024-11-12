if(CONFIG_VALUE_SYNC)
  zephyr_library()

  zephyr_library_sources(sync.c)
  zephyr_library_sources_ifdef(CONFIG_VALUE_SYNC_SHELL
    sync_shell.c
  )
endif()
