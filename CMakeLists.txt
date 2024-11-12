list(APPEND SYSCALL_INCLUDE_DIRS ${ZEPHYR_CURRENT_MODULE_DIR}/include)
set(SYSCALL_INCLUDE_DIRS ${SYSCALL_INCLUDE_DIRS} PARENT_SCOPE)

zephyr_include_directories(${ZEPHYR_CURRENT_MODULE_DIR}/include)

add_subdirectory(drivers)
#add_subdirectory(subsys)
#add_subdirectory(lib)
