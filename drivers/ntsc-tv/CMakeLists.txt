add_library(graphics INTERFACE)
target_link_libraries(graphics INTERFACE hardware_dma hardware_gpio)
target_include_directories(graphics INTERFACE ${CMAKE_CURRENT_LIST_DIR})
