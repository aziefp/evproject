add_executable(test_protocol test_protocol.cpp)
target_link_libraries(test_protocol PRIVATE ev_common Qt6::Test)

add_test(NAME protocol COMMAND test_protocol)

add_executable(test_database
    test_database.cpp
    ../apps/admin-server/database.cpp
    ../apps/admin-server/database.h
)
target_include_directories(test_database PRIVATE ../apps/admin-server)
target_link_libraries(test_database PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
add_test(NAME database COMMAND test_database)
