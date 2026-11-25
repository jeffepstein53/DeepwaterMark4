# Tests of the pace rules (news/ActualsPace.h, positioning/CotPace.h): pure arithmetic, nothing leaves the machine.
#   cmake -S tests/freshness -B build-tests-freshness -G Ninja -DCMAKE_PREFIX_PATH=$HOME/Qt/6.12.0/macos && cmake --build build-tests-freshness
#   ./build-tests-freshness/freshness_tests
cmake_minimum_required(VERSION 3.21)
project(FreshnessTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
find_package(Qt6 REQUIRED COMPONENTS Core)
set(DW_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(freshness_tests ${CMAKE_CURRENT_SOURCE_DIR}/freshness_tests.cpp)
target_include_directories(freshness_tests PRIVATE ${DW_ROOT}/src)
target_link_libraries(freshness_tests PRIVATE Qt6::Core)
