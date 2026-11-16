add_library(flash-config INTERFACE)
target_sources(flash-config INTERFACE ${CMAKE_CURRENT_LIST_DIR}/flash_config.c)
target_include_directories(flash-config INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(flash-config INTERFACE pico_bootrom hardware_sync hardware_timer)
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/flash_config.c
        DIRECTORY ${PROJECT_SOURCE_DIR}
        PROPERTIES COMPILE_OPTIONS "-O2;-fno-jump-tables;-fno-tree-loop-distribute-patterns")
