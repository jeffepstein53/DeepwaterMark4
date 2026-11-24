# Tests of Data › Technical data (src/technicaldata): technical-data.sqlite and the Binance collector, with a fake Binance (nothing
# leaves the machine).
#   cmake -S tests/technicaldata -B build-tests-technicaldata -G Ninja -DCMAKE_PREFIX_PATH=$HOME/Qt/6.12.0/macos && cmake --build build-tests-technicaldata
#   ./build-tests-technicaldata/technicaldata_tests [filter]
cmake_minimum_required(VERSION 3.21)
project(TechnicalDataTests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
find_package(Qt6 REQUIRED COMPONENTS Core Sql Network Concurrent)
set(DW_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
# src/charts/core, the indicator engine (the indicators are its values)
add_compile_options($<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-ffp-contract=off>)
add_subdirectory(${DW_ROOT}/third_party/quickjs-ng ${CMAKE_CURRENT_BINARY_DIR}/quickjs-ng)
add_subdirectory(${DW_ROOT}/third_party/lzma ${CMAKE_CURRENT_BINARY_DIR}/lzma)
add_subdirectory(${DW_ROOT}/third_party/miniz ${CMAKE_CURRENT_BINARY_DIR}/miniz)
file(GLOB CHARTS_CORE CONFIGURE_DEPENDS ${DW_ROOT}/src/charts/core/*.cpp ${DW_ROOT}/src/charts/core/*.h)
add_executable(technicaldata_tests ${CHARTS_CORE}
  ${DW_ROOT}/src/technicaldata/TechnicalDataIndicators.cpp ${DW_ROOT}/src/technicaldata/TechnicalDataIndicators.h
  ${DW_ROOT}/src/technicaldata/TechnicalDataSpec.cpp ${DW_ROOT}/src/technicaldata/TechnicalDataSpec.h
  ${DW_ROOT}/src/technicaldata/Dukascopy.cpp ${DW_ROOT}/src/technicaldata/Dukascopy.h
  ${DW_ROOT}/src/technicaldata/ForexCollector.cpp ${DW_ROOT}/src/technicaldata/ForexCollector.h
  ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ${CMAKE_CURRENT_SOURCE_DIR}/technicaldata_tests.cpp ${CMAKE_CURRENT_SOURCE_DIR}/footprint_archive_tests.cpp ${CMAKE_CURRENT_SOURCE_DIR}/perp_archive_tests.cpp
  ${DW_ROOT}/src/technicaldata/PerpArchive.cpp ${DW_ROOT}/src/technicaldata/PerpArchive.h
  ${DW_ROOT}/src/technicaldata/LiveFootprint.cpp ${DW_ROOT}/src/technicaldata/LiveFootprint.h
  ${DW_ROOT}/src/technicaldata/OrderFlowFeatures.cpp ${DW_ROOT}/src/technicaldata/OrderFlowFeatures.h
  ${DW_ROOT}/src/technicaldata/FootprintArchive.cpp ${DW_ROOT}/src/technicaldata/FootprintArchive.h
  ${DW_ROOT}/src/technicaldata/ArchiveCollector.cpp ${DW_ROOT}/src/technicaldata/ArchiveCollector.h
  ${DW_ROOT}/src/technicaldata/TechnicalDataStore.cpp ${DW_ROOT}/src/technicaldata/TechnicalDataStore.h
  ${DW_ROOT}/src/technicaldata/Collector.cpp ${DW_ROOT}/src/technicaldata/Collector.h
  ${DW_ROOT}/src/net/Http.cpp ${DW_ROOT}/src/net/Http.h ${DW_ROOT}/src/net/Sources.cpp ${DW_ROOT}/src/net/Sources.h
  ${DW_ROOT}/src/core/Settings.cpp ${DW_ROOT}/src/core/Paths.cpp)
target_include_directories(technicaldata_tests PRIVATE ${DW_ROOT}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(technicaldata_tests PRIVATE Qt6::Core Qt6::Sql Qt6::Network Qt6::Concurrent quickjs lzmadec miniz)
qt_add_resources(technicaldata_tests technicaldata_pinets PREFIX "/pinets" BASE ${DW_ROOT}/third_party/pinets FILES ${DW_ROOT}/third_party/pinets/pinets.js)
