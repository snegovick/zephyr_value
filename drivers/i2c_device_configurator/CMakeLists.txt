if(CONFIG_I2C_DEVICE_CONFIGURATOR)
  zephyr_library()

  zephyr_library_sources(i2c_device_configurator.c)
endif()
