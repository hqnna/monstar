cmake_minimum_required(VERSION 3.21)

project(monstar-qt VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Test)

qt_standard_project_setup(REQUIRES 6.8)

# The split layout is pure geometry, shared by the host and its tests.
add_library(monstar-split-tree STATIC src/SplitTree.cpp src/SplitTree.h)
target_link_libraries(monstar-split-tree PUBLIC Qt6::Core)
target_include_directories(monstar-split-tree PUBLIC src)

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(tst_splittree tests/tst_splittree.cpp)
    target_link_libraries(tst_splittree PRIVATE monstar-split-tree Qt6::Test)
    add_test(NAME tst_splittree COMMAND tst_splittree)
endif()
