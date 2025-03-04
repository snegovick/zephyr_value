if(CONFIG_POWER_GRAPH)
  zephyr_library()

  zephyr_library_sources(power_graph.c)

  zephyr_library_sources_ifdef(CONFIG_POWER_GRAPH_SHELL
    power_graph_shell.c
  )
endif()
