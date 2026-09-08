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
include(GNUInstallDirs)

add_subdirectory(libs/common)
add_subdirectory(apps/admin-server)
add_subdirectory(apps/user-client)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

install(TARGETS ev-admin-server ev-user-client
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Generate a self-contained Qt runtime tree during `cmake --install`.  The user
# client deployment hook also installs QtWebEngineProcess, Chromium resources,
# and locale files; the second script adds the admin-only Qt modules/plugins.
qt_generate_deploy_app_script(
    TARGET ev-user-client
    OUTPUT_SCRIPT deploy_user_client
    INCLUDE_PLUGINS qoffscreen
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_user_client})

qt_generate_deploy_app_script(
    TARGET ev-admin-server
    OUTPUT_SCRIPT deploy_admin_server
    NO_TRANSLATIONS
    EXCLUDE_PLUGINS
        qsqlibase
        qsqlmimer
        qsqlmysql
        qsqloci
        qsqlodbc
        qsqlpsql
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_admin_server})

install(PROGRAMS
    packaging/linux-portable/运行演示.sh
    packaging/linux-portable/检查完整性.sh
    DESTINATION .
)
install(FILES
    packaging/linux-portable/运行说明.txt
    DESTINATION .
)

# Qt's WebEngine deploy hook writes an absolute Prefix into libexec/qt.conf on
# Linux.  Replace both files last so the finished directory remains movable.
install(FILES packaging/linux-portable/qt.conf DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/linux-portable/qt.conf DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
