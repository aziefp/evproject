cmake_minimum_required(VERSION 3.21)

project(EVChargingPlatform VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS
    Core
    Widgets
    Network
    Sql
    Charts
    WebEngineWidgets
    Test
)

include(CTest)

add_subdirectory(libs/common)
add_subdirectory(apps/admin-server)
add_subdirectory(apps/user-client)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
