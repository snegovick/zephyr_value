file(GLOB modules_paths "${CMAKE_CURRENT_LIST_DIR}/*/CMakeLists.txt")

foreach(module_path ${modules_paths})
  get_filename_component(module_dir ${module_path} DIRECTORY)
  get_filename_component(module_name ${module_dir} NAME)

  add_subdirectory(${module_name})
endforeach()
