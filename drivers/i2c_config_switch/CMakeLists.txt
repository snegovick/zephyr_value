if(CONFIG_I2C_CONFIG_SWITCH)
  zephyr_library()

  zephyr_library_sources(i2c_config_switch.c)
endif()
