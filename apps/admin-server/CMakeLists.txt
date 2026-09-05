qt_add_executable(ev-admin-server
    main.cpp
    adminwindow.cpp
    adminwindow.h
    database.cpp
    database.h
    serverworker.cpp
    serverworker.h
)

target_link_libraries(ev-admin-server PRIVATE
    ev_common
    Qt6::Widgets
    Qt6::Network
    Qt6::Sql
    Qt6::Charts
)

set_target_properties(ev-admin-server PROPERTIES OUTPUT_NAME "ev-admin-server")
