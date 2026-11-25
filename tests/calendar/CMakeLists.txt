# Tests of the calendar's figures: the statistics offices' answers (macro/Official), the official series of a past release
# (news/History), the wire's headlines and the actuals feed (news/Calendar), the national macro series (macro/National), the tables of rate odds (macro/RateTables), the real rates and the carry (fundamental),
# a figure against its other sources (macro/CrossCheck), the sources that stop answering (net/Silent), the encrypted
# settings (core/Secret). The network, the folders and the settings are
# stubs in the test file: nothing leaves the machine and no user file is touched.
#   cmake -S tests/calendar -B build-tests-calendar -G Ninja -DCMAKE_PREFIX_PATH=$HOME/Qt/6.12.0/macos && cmake --build build-tests-calendar
#   ./build-tests-calendar/calendar_tests
cmake_minimum_required(VERSION 3.21)
project(CalendarTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
find_package(Qt6 REQUIRED COMPONENTS Core Concurrent Network)
set(DW_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(calendar_tests ${CMAKE_CURRENT_SOURCE_DIR}/calendar_tests.cpp ${DW_ROOT}/src/macro/Official.cpp ${DW_ROOT}/src/macro/National.cpp ${DW_ROOT}/src/macro/RateTables.cpp
  ${DW_ROOT}/src/macro/CrossCheck.cpp ${DW_ROOT}/src/macro/MeetingRates.cpp ${DW_ROOT}/src/net/Silent.cpp ${DW_ROOT}/src/core/Secret.cpp
  ${DW_ROOT}/src/fundamental/Fundamental.cpp ${DW_ROOT}/src/fundamental/Fundamental.h
  ${DW_ROOT}/src/news/History.cpp ${DW_ROOT}/src/news/History.h ${DW_ROOT}/src/news/Calendar.cpp ${DW_ROOT}/src/news/Calendar.h
  ${DW_ROOT}/src/news/DataHeadline.cpp)
if(MSVC)
  target_compile_options(calendar_tests PRIVATE /utf-8)
endif()
target_include_directories(calendar_tests PRIVATE ${DW_ROOT}/src)
target_link_libraries(calendar_tests PRIVATE Qt6::Core Qt6::Concurrent Qt6::Network)
if(WIN32)
  target_link_libraries(calendar_tests PRIVATE crypt32)
endif()
