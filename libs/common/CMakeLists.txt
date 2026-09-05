add_library(ev_common STATIC
    protocol.cpp
    protocol.h
)

target_include_directories(ev_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ev_common PUBLIC Qt6::Core)

target_compile_definitions(ev_common PUBLIC
    EV_PROTOCOL_VERSION=1
)
