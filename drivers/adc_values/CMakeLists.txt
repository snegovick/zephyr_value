if(CONFIG_ADC_VALUES)
  zephyr_library()

  zephyr_library_sources(adc_values.c)

  zephyr_library_sources_ifdef(CONFIG_ADC_VALUES_SHELL adc_values_shell.c)
endif()
