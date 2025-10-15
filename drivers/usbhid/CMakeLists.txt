add_library(usbhid INTERFACE)

target_sources(usbhid INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/hid_app.c
        ${CMAKE_CURRENT_LIST_DIR}/hid_app.h
)

target_link_libraries(usbhid INTERFACE hardware_pio hardware_clocks)

target_include_directories(usbhid INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
)


#pico_generate_pio_header(usbhid 
#  ${CMAKE_CURRENT_LIST_DIR}/usbhid_mrmltr.pio
#)
