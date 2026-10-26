cmake_minimum_required(VERSION 3.21)

# C compiles wayland-scanner's protocol tables.
project(monstar-qt VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick WaylandCompositor Test)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WaylandServer REQUIRED IMPORTED_TARGET wayland-server)
pkg_check_modules(WaylandClient REQUIRED IMPORTED_TARGET wayland-client)
# Optional: blur behind translucent windows on KDE Plasma.
find_package(KF6WindowSystem QUIET)

qt_standard_project_setup(REQUIRES 6.8)

# The split layout is pure geometry, shared by the host and its tests.
add_library(monstar-split-tree STATIC src/SplitTree.cpp src/SplitTree.h)
target_link_libraries(monstar-split-tree PUBLIC Qt6::Core)
target_include_directories(monstar-split-tree PUBLIC src)

qt_add_executable(monstar-qt src/main.cpp)

qt_add_qml_module(monstar-qt
    URI Monstar.Host
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/TabStrip.qml
    SOURCES
        src/HostConfig.cpp
        src/HostConfig.h
        src/KeyboardForwarder.cpp
        src/KeyboardForwarder.h
        src/MonstarCompositor.cpp
        src/MonstarCompositor.h
        src/TerminalHost.cpp
        src/TerminalHost.h
)

qt_generate_wayland_protocol_server_sources(monstar-qt
    PRIVATE_CODE
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/../protocol/monstar-host-v1.xml
)

target_link_libraries(monstar-qt PRIVATE
    monstar-split-tree
    Qt6::Quick
    Qt6::WaylandCompositor
    PkgConfig::WaylandServer
    PkgConfig::WaylandClient
)
if(KF6WindowSystem_FOUND)
    target_link_libraries(monstar-qt PRIVATE KF6::WindowSystem)
    target_compile_definitions(monstar-qt PRIVATE MONSTAR_HAVE_KWINDOWSYSTEM=1)
else()
    message(STATUS "KF6WindowSystem not found: background-blur is unavailable in monstar-qt")
endif()

install(TARGETS monstar-qt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(tst_splittree tests/tst_splittree.cpp)
    target_link_libraries(tst_splittree PRIVATE monstar-split-tree Qt6::Test)
    add_test(NAME tst_splittree COMMAND tst_splittree)

    qt_add_executable(tst_hostconfig tests/tst_hostconfig.cpp src/HostConfig.cpp src/HostConfig.h)
    target_include_directories(tst_hostconfig PRIVATE src)
    target_link_libraries(tst_hostconfig PRIVATE Qt6::Gui Qt6::Qml Qt6::Test)
    add_test(NAME tst_hostconfig COMMAND tst_hostconfig)
endif()
