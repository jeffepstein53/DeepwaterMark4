# Tests of three small guards (0.4.3): where Claude Desktop's configuration files are and how the "deepwater" entry is put in and taken
# out (ai/ClaudeDesktopFiles.h), the one-Deepwater-per-profile lock (app/SingleInstance.h) and the removal of retired settings
# (core/Settings.cpp). Made-up folders and a test profile only: no user file, no setting of the user is touched.
#   cmake -S tests/guards -B build-tests-guards -G Ninja -DCMAKE_PREFIX_PATH=$HOME/Qt/6.12.0/macos && cmake --build build-tests-guards
#   ./build-tests-guards/guards_tests
cmake_minimum_required(VERSION 3.21)
project(GuardsTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
find_package(Qt6 REQUIRED COMPONENTS Core)
set(DW_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(guards_tests ${CMAKE_CURRENT_SOURCE_DIR}/guards_tests.cpp ${DW_ROOT}/src/core/Settings.cpp ${DW_ROOT}/src/core/Secret.cpp)
target_include_directories(guards_tests PRIVATE ${DW_ROOT}/src)
target_link_libraries(guards_tests PRIVATE Qt6::Core)
if(WIN32)
  target_link_libraries(guards_tests PRIVATE crypt32)
endif()
