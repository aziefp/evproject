qt_add_executable(ev-user-client
    main.cpp
    clientconnection.cpp
    clientconnection.h
    mapdialog.cpp
    mapdialog.h
    userwindow.cpp
    userwindow.h
)

target_link_libraries(ev-user-client PRIVATE
    ev_common
    Qt6::Widgets
    Qt6::Network
    Qt6::WebEngineWidgets
)

set_target_properties(ev-user-client PROPERTIES OUTPUT_NAME "ev-user-client")
