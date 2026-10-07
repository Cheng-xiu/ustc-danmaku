add_executable(sim main.c log.c)
set_source_files_properties(main.c log.c PROPERTIES LANGUAGE C)
target_link_libraries(sim PRIVATE demo_core)
target_include_directories(sim PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
