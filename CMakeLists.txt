cmake_minimum_required(VERSION 3.21)
project(Deepwater VERSION 0.4.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
# The organization name that places the settings (Windows: HKCU\Software\<name>\Deepwater) and the user data
# (%APPDATA%\<name>\Deepwater). A build run next to an installed Deepwater gets its own with -DDW_ORGANIZATION=<other>,
# so the two never read or write each other's settings and data. Deepwater Mark 4 (2026-11-24) has its own by default,
# the one of the corrected copy (deepwater-terminal-corrige); -DDW_ORGANIZATION=deepwater-terminal gives the original's.
set(DW_ORGANIZATION "deepwater-terminal-corrige" CACHE STRING "Organization name of the settings and the user data")

# MSVC, for every target: sources are UTF-8, and __cplusplus must give the real standard (MSVC reports 199711 without
# /Zc:__cplusplus).
if(MSVC)
  add_compile_options(/utf-8 $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Svg Concurrent Xml Sql OpenGL OpenGLWidgets LinguistTools WebSockets HttpServer)   # HttpServer: the Squawk player's page (pages/news/squawk)
enable_language(C)
if(APPLE)
  enable_language(OBJC OBJCXX)
endif()

# Every .cpp under src/ is part of the app: new pages and services need no edit here.
file(GLOB_RECURSE DW_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
# Deepwater Mark 4 has neither the Charts tab nor the AI tab (removed 2026-11-24; their code is in the repository's history).
# What other pages need from them stays: src/charts/core and four files of src/charts/data (Data › Technical data's
# indicators, footprints and Binance streams). src/ai is Mark 4's MCP server for Claude (Options > Claude): its data tools,
# read only, and the bridge `Deepwater --mcp-stdio` that Claude Code and Claude Desktop start. Desktop builds only.
if(IOS)
  list(FILTER DW_SOURCES EXCLUDE REGEX "/src/ai/")
endif()
if(APPLE)
  file(GLOB_RECURSE DW_OBJC CONFIGURE_DEPENDS src/*.mm)
  if(IOS)
    # iOS: the squawk's web view has its UIKit host (NativeTvIos.mm), not the macOS one (NativeTv.mm: NSView)
    list(FILTER DW_OBJC EXCLUDE REGEX "NativeTv\\.mm$")
  endif()
  list(APPEND DW_SOURCES ${DW_OBJC})
  # PDFKit and Vision objects are allocated with alloc/init and never released by hand: without ARC each PDF text
  # extraction, print and OCR leaked its document / request. NativeTv.mm keeps its objects in void* members with
  # manual retain/release (correct as written) and stays without ARC; a new .mm file should be added here.
  set_source_files_properties(src/banks/Pdf.mm src/macro/Ocr.mm src/platform/IosNotify.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()

# JavaScript engine of src/charts/core (CustomJs, Pine): QuickJS-ng vendored in
# third_party/quickjs-ng (library sources only, MIT, see its LICENSE and SOURCES.md): the static C library `quickjs`,
# built from its own folder, outside the src/ glob.
add_subdirectory(third_party/quickjs-ng)
# third_party/lzma: the LZMA SDK's decoder (public domain), for Dukascopy's .bi5 files (Data › Technical data, forex)
add_subdirectory(third_party/lzma)
# third_party/miniz: its streaming inflater (MIT), for Binance's trade archives (Data › Technical data, footprint history)
add_subdirectory(third_party/miniz)

qt_add_executable(Deepwater MACOSX_BUNDLE ${DW_SOURCES})
# src/charts (port of Quant Terminal, docs/charts/NOTICE.md): same floating-point results as the JS, so no fused
# multiply-add there.
# src/technicaldata computes the same indicators into Data › Technical data: its tests build it with -ffp-contract=off
# (tests/technicaldata/CMakeLists.txt), so the app does too, or the shipped values could differ from the tested ones.
file(GLOB_RECURSE DW_CHARTS_SOURCES CONFIGURE_DEPENDS src/charts/*.cpp src/technicaldata/*.cpp)
if(NOT MSVC)
  set_source_files_properties(${DW_CHARTS_SOURCES} PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()
target_include_directories(Deepwater PRIVATE src)
# MSVC: sources are UTF-8 (labels hold "→", "·", "€"…), some translation units exceed the default section count, and Qt
# wants the real __cplusplus. On Windows the app is a GUI program (no console window behind it).
target_compile_options(Deepwater PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /bigobj /Zc:__cplusplus /permissive->)
target_compile_definitions(Deepwater PRIVATE $<$<CXX_COMPILER_ID:MSVC>:NOMINMAX _USE_MATH_DEFINES>)
if(WIN32)
  set_target_properties(Deepwater PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_libraries(Deepwater PRIVATE opengl32)   # Macro data's Landscape calls glClear / glClearColor directly
  target_link_libraries(Deepwater PRIVATE crypt32 winmm)   # core/Secret: Windows encrypts the Myfxbook session (DPAPI); platform/WinNotify: the alerts' sound
  # Native parity with macOS (2026-11-22): Bank Research's PDF pages and printing (Qt PDF = PDFium, Qt Print Support;
  # platform/WinPdf.cpp), the Squawk pane's live player (Qt WebView's WebView2 backend; NativeTvWin.cpp), and the RBA /
  # RBNZ pictures read by Windows' own OCR through C++/WinRT (windowsapp; platform/WinOcr.cpp).
  find_package(Qt6 REQUIRED COMPONENTS Pdf PrintSupport WebView)
  target_link_libraries(Deepwater PRIVATE Qt6::Pdf Qt6::PrintSupport Qt6::WebView windowsapp)
  # the Deepwater logo as the icon of Deepwater.exe, its shortcuts, its title bars and its taskbar button: Qt gives
  # every window the resource named IDI_ICON1 (windows/Deepwater.ico, made by tools/app_icons from
  # resources/icon/deepwater.svg)
  target_sources(Deepwater PRIVATE windows/Deepwater.rc)
  # Deepwater Mark 4 (2026-11-24): the file's details in Explorer (description, product, version, licence), from
  # windows/version.rc.in with the project's version
  configure_file(windows/version.rc.in ${CMAKE_BINARY_DIR}/Deepwater-version.rc @ONLY)
  target_sources(Deepwater PRIVATE ${CMAKE_BINARY_DIR}/Deepwater-version.rc)
  # binaries that are published: the assert messages (QT_FORCE_ASSERTS below) name Qt's headers and the sources
  # without the builder's folders (C:\Users\<name>\…). MSVC's /d1trimfile removes these prefixes from __FILE__, as
  # -ffile-prefix-map does for iOS below.
  if(MSVC)
    get_filename_component(DW_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
    file(TO_NATIVE_PATH "${DW_QT_PREFIX}" DW_QT_PREFIX_NATIVE)
    file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}" DW_SOURCE_NATIVE)
    file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}" DW_BINARY_NATIVE)
    target_compile_options(Deepwater PRIVATE "/d1trimfile:${DW_QT_PREFIX_NATIVE}" "/d1trimfile:${DW_SOURCE_NATIVE}"
                                             "/d1trimfile:${DW_BINARY_NATIVE}")
  endif()
endif()
# Hardening (audit 2026-11-18, B1; user's choice 2026-11-19): out-of-range accesses in the standard containers
# (std::vector, std::span, std::string, std::optional…) and in Qt's (QList, QString…) stop the app at the faulty line
# instead of reading or writing memory at random — a clean crash report instead of silent corruption or a crash
# elsewhere later. libc++ "fast" mode: the cheap checks only. QT_FORCE_ASSERTS: Qt's own Q_ASSERT bounds checks in its
# inline container code, otherwise compiled out of every non-Debug build (QT_NO_DEBUG). The cost was measured on the
# heaviest Charts frames. -DDW_HARDENING=OFF builds without, for comparison.
option(DW_HARDENING "Bounds checks in the standard library and Qt containers" ON)
if(DW_HARDENING)
  target_compile_definitions(Deepwater PRIVATE
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST>
    $<$<CXX_COMPILER_ID:MSVC>:_MSVC_STL_HARDENING=1>
    QT_FORCE_ASSERTS)
endif()
# Compiler warnings on Deepwater's own code (third_party/ builds in its own targets without them). Kept at zero: a new
# one is seen. missing-field-initializers is off: `Row{a, b}` leaving the other fields at their defaults is the house
# style (263 of the 303 warnings when this was turned on, none a bug).
target_compile_options(Deepwater PRIVATE $<$<COMPILE_LANG_AND_ID:CXX,AppleClang,Clang,GNU>:-Wall -Wextra -Wno-missing-field-initializers>
                                         $<$<COMPILE_LANG_AND_ID:OBJCXX,AppleClang,Clang>:-Wall -Wextra -Wno-missing-field-initializers>
                                         $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4>)
target_link_libraries(Deepwater PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Svg Qt6::Concurrent Qt6::Xml Qt6::Sql Qt6::OpenGL Qt6::OpenGLWidgets Qt6::WebSockets Qt6::HttpServer quickjs lzmadec miniz)
if(IOS)
  target_link_libraries(Deepwater PRIVATE "-framework Foundation" "-framework UIKit" "-framework Vision" "-framework PDFKit" "-framework CoreGraphics"
                        "-framework WebKit" "-framework UserNotifications")   # the Squawk player, alert notifications
  # Qt 6.12.0's RHI compositor with the upstream fix for QTBUG-151881 (whole window copied on every repaint): Qt is
  # static on iOS, so this file's definitions replace the library's (third_party/qt-patches/SOURCES.md)
  # binaries that are published (the iOS release): source and Qt header paths in assert messages written without the
  # builder's home folder
  target_compile_options(Deepwater PRIVATE "-ffile-prefix-map=${CMAKE_SOURCE_DIR}/=" "-ffile-prefix-map=$ENV{HOME}/=~/")
  find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
  target_sources(Deepwater PRIVATE third_party/qt-patches/qbackingstoredefaultcompositor.cpp)
  target_link_libraries(Deepwater PRIVATE Qt6::GuiPrivate)
elseif(APPLE)
  target_link_libraries(Deepwater PRIVATE "-framework Foundation" "-framework AppKit" "-framework Vision" "-framework PDFKit" "-framework CoreGraphics" "-framework WebKit")
endif()

# Every file under resources/ is embedded at :/<path relative to resources>.
file(GLOB_RECURSE DW_RESOURCES CONFIGURE_DEPENDS RELATIVE ${CMAKE_SOURCE_DIR}/resources resources/*)
list(FILTER DW_RESOURCES EXCLUDE REGEX "(^|/)\\.DS_Store$|(^|/)SOURCES[^/]*$|\\.md$|(^|/)__pycache__/|\\.pyc$")
# ...except the documentation's chapters (app/HelpWindow), which are Markdown
file(GLOB_RECURSE DW_HELP_PAGES CONFIGURE_DEPENDS RELATIVE ${CMAKE_SOURCE_DIR}/resources resources/help/*.md)
list(APPEND DW_RESOURCES ${DW_HELP_PAGES})
set(DW_RESOURCES_ABS "")
foreach(r ${DW_RESOURCES})
  list(APPEND DW_RESOURCES_ABS ${CMAKE_SOURCE_DIR}/resources/${r})
endforeach()
qt_add_resources(Deepwater dw_assets PREFIX "/" BASE resources FILES ${DW_RESOURCES_ABS})
# Pine Script runtime of src/charts/core/Pine: PineTS vendored in third_party/pinets (AGPL-3.0, see
# its LICENSE and SOURCES.md; Deepwater is AGPL-3.0 because it ships it), embedded at :/pinets/pinets.js.
qt_add_resources(Deepwater dw_pinets PREFIX "/pinets" BASE third_party/pinets FILES third_party/pinets/pinets.js)

if(IOS)
  # iPhone and iPad (1, 2); the window's minimum (960 x 540) fits an iPad in landscape
  set_target_properties(Deepwater PROPERTIES XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
                                             XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER com.deepwater-terminal.Deepwater
                                             XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon
                                             MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
                                             XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION ${PROJECT_VERSION}
                                             XCODE_ATTRIBUTE_MARKETING_VERSION ${PROJECT_VERSION})
  # the home screen icon: the Deepwater logo as a full square (iOS rounds it), made by tools/app_icons from
  # resources/icon/deepwater.svg; compiled by Xcode's asset catalog step (the Xcode generator, build-ios-xcode)
  target_sources(Deepwater PRIVATE ios/Assets.xcassets)
  set_source_files_properties(ios/Assets.xcassets PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()
set_target_properties(Deepwater PROPERTIES
  MACOSX_BUNDLE_GUI_IDENTIFIER com.deepwater-terminal.Deepwater
  MACOSX_BUNDLE_BUNDLE_NAME Deepwater
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
  MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
# The app's version (Help › the update check compares it with the releases' tags): the project's, set once above.
# (main.cpp only: a new version recompiles one file, not the app)
set_source_files_properties(src/main.cpp PROPERTIES COMPILE_DEFINITIONS
                            "DW_VERSION=\"${PROJECT_VERSION}\";DW_ORGANIZATION=\"${DW_ORGANIZATION}\"")
if(APPLE AND NOT IOS)
  # the Dock / Finder icon: the Deepwater logo at every size macOS asks for (macos/Deepwater.icns, made by
  # tools/app_icons from resources/icon/deepwater.svg)
  set_target_properties(Deepwater PROPERTIES MACOSX_BUNDLE_ICON_FILE Deepwater.icns)
  target_sources(Deepwater PRIVATE macos/Deepwater.icns)
  set_source_files_properties(macos/Deepwater.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  # ---- macOS package: build/dist/Deepwater-<version>-macos.dmg (cmake/MacDist.cmake)
  find_program(DW_MACDEPLOYQT macdeployqt HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" "${Qt6_DIR}/../../../bin")
  set(DW_DIST_DEPS Deepwater)
  if(TARGET dw_translate)
    list(APPEND DW_DIST_DEPS dw_translate)
  endif()
  add_custom_target(dist
    COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_BUNDLE_DIR:Deepwater> -DOUT=${CMAKE_BINARY_DIR}/dist
            -DVERSION=${PROJECT_VERSION} -DMACDEPLOYQT=${DW_MACDEPLOYQT} -P ${CMAKE_SOURCE_DIR}/cmake/MacDist.cmake
    DEPENDS ${DW_DIST_DEPS} VERBATIM)
endif()

# On-device EN→FR translation helper (Apple Translation framework), built next to the executable. Not on iOS: an iOS
# app cannot start another program.
if(APPLE AND NOT IOS)
  find_program(DW_SWIFTC swiftc)
  if(DW_SWIFTC)
    set(DW_TRANSLATE_SRC ${CMAKE_SOURCE_DIR}/src/news/translate/dw-translate.swift)
    set(DW_TRANSLATE_BIN ${CMAKE_BINARY_DIR}/Deepwater.app/Contents/MacOS/dw-translate)
    add_custom_command(OUTPUT ${DW_TRANSLATE_BIN}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/Deepwater.app/Contents/MacOS
      COMMAND ${DW_SWIFTC} -parse-as-library -O ${DW_TRANSLATE_SRC} -o ${DW_TRANSLATE_BIN}
      DEPENDS ${DW_TRANSLATE_SRC} COMMENT "Building dw-translate")
    add_custom_target(dw_translate ALL DEPENDS ${DW_TRANSLATE_BIN})
    add_dependencies(Deepwater dw_translate)
  endif()
endif()
# iOS (native parity, 2026-11-22): no helper process, so the same translator runs in the app — a static library built
# from src/news/translate/DWTranslateIOS.swift with swiftc and linked into Deepwater; its objects name the Swift runtime
# libraries they need (found in the SDK, the runtime itself is the system's) and the Translation framework.
if(IOS)
  set(DW_TR_IOS_SRC ${CMAKE_SOURCE_DIR}/src/news/translate/DWTranslateIOS.swift)
  set(DW_TR_IOS_LIB ${CMAKE_BINARY_DIR}/libdwtranslate-ios.a)
  set(DW_TR_IOS_MIN ${CMAKE_OSX_DEPLOYMENT_TARGET})
  if(NOT DW_TR_IOS_MIN)
    set(DW_TR_IOS_MIN 18.0)
  endif()
  execute_process(COMMAND xcrun --sdk iphoneos --show-sdk-path OUTPUT_VARIABLE DW_IOS_SDK OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND xcrun --sdk iphoneos --find swiftc OUTPUT_VARIABLE DW_IOS_SWIFTC OUTPUT_STRIP_TRAILING_WHITESPACE)
  get_filename_component(DW_IOS_TOOLCHAIN_LIB "${DW_IOS_SWIFTC}/../../lib/swift/iphoneos" ABSOLUTE)
  add_custom_command(OUTPUT ${DW_TR_IOS_LIB}
    COMMAND ${DW_IOS_SWIFTC} -parse-as-library -O -emit-library -static -module-name DWTranslate
            -target arm64-apple-ios${DW_TR_IOS_MIN} -sdk ${DW_IOS_SDK} ${DW_TR_IOS_SRC} -o ${DW_TR_IOS_LIB}
    DEPENDS ${DW_TR_IOS_SRC} COMMENT "Building the in-app translator (DWTranslateIOS.swift)")
  add_custom_target(dw_translate_ios DEPENDS ${DW_TR_IOS_LIB})
  add_dependencies(Deepwater dw_translate_ios)
  target_link_libraries(Deepwater PRIVATE ${DW_TR_IOS_LIB} "-L${DW_IOS_SDK}/usr/lib/swift" "-L${DW_IOS_TOOLCHAIN_LIB}"
                        "-framework Translation")
endif()

# French translations: one Qt Linguist file per package (translations/<package>_fr.ts), compiled to .qm and embedded
# at :/i18n. Update a package's file with: lupdate <its source folders> -ts translations/<package>_fr.ts -no-obsolete
file(GLOB DW_TS_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/translations/*_fr.ts)
if(DW_TS_FILES)
  set_source_files_properties(${DW_TS_FILES} PROPERTIES OUTPUT_LOCATION ${CMAKE_BINARY_DIR}/i18n)
  qt_add_lrelease(TS_FILES ${DW_TS_FILES} QM_FILES_OUTPUT_VARIABLE DW_QM_FILES)
  qt_add_resources(Deepwater dw_i18n PREFIX "/i18n" BASE ${CMAKE_BINARY_DIR}/i18n FILES ${DW_QM_FILES})
endif()

# ---- Windows package: the app, Qt's DLLs and plugins (windeployqt), the VC++ runtime beside them, and
# an NSIS installer (cpack -G NSIS, or the `dist` target). Everything at the top of the install folder: Qt's deploy
# script would otherwise put the DLLs in bin/ apart from the executable.
if(WIN32)
  set(DW_WIN_TARGETS Deepwater)
  set(CMAKE_INSTALL_BINDIR ".")
  include(GNUInstallDirs)
  install(TARGETS ${DW_WIN_TARGETS} RUNTIME DESTINATION .)
  # the licences of Deepwater and of what it ships, beside the program
  install(DIRECTORY licences/ DESTINATION licences)
  install(FILES LICENSE DESTINATION licences RENAME Deepwater-AGPL-3.0.txt)
  install(FILES third_party/pinets/LICENSE DESTINATION licences RENAME PineTS-LICENSE.txt)
  install(FILES third_party/quickjs-ng/LICENSE DESTINATION licences RENAME QuickJS-ng-LICENSE.txt)
  install(FILES third_party/miniz/LICENSE DESTINATION licences RENAME miniz-LICENSE.txt)
  install(FILES docs/charts/NOTICE.md DESTINATION licences RENAME Indicateurs-NOTICE.md)
  qt_generate_deploy_app_script(TARGET Deepwater OUTPUT_SCRIPT DW_DEPLOY_SCRIPT NO_TRANSLATIONS)
  # Qt's deploy script puts the DLLs in QT_DEPLOY_BIN_DIR, which defaults to the CMAKE_INSTALL_BINDIR seen by
  # find_package(Qt6) ("bin"), where Windows wouldn't look for them: next to the exe instead.
  install(CODE "set(QT_DEPLOY_BIN_DIR .)")
  install(SCRIPT ${DW_DEPLOY_SCRIPT})
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
  set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
  include(InstallRequiredSystemLibraries)

  # Deepwater Mark 4: published as ONE .exe (windows/installer.nsi, made by NSIS: `cmake --build <build> --target installer`),
  # per user, without an administrator's consent. The .zip cpack makes is the staging folder the installer is packed
  # from, and what the `dist` target renames.
  set(CPACK_GENERATOR "ZIP")
  set(CPACK_PACKAGE_NAME "Deepwater Mark 4")
  set(CPACK_PACKAGE_VENDOR "Deepwater Mark 4")
  set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
  set(CPACK_PACKAGE_FILE_NAME "Deepwater-Mark4-${PROJECT_VERSION}-win64")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Deepwater Mark 4 market terminal")
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "Deepwater Mark 4")
  set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_BINARY_DIR}/dist")
  # the .zip's own settings (windows/cpack-options.cmake): its inner folder keeps the name Deepwater-Mark4 from one
  # version to the next, so a new version unzipped over the previous one replaces its files
  set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_SOURCE_DIR}/windows/cpack-options.cmake")
  include(CPack)
  add_custom_target(dist COMMAND ${CMAKE_CPACK_COMMAND} --config ${CMAKE_BINARY_DIR}/CPackConfig.cmake
                    COMMAND ${CMAKE_COMMAND} -E rename ${CMAKE_BINARY_DIR}/dist/Deepwater-Mark4.zip
                            ${CMAKE_BINARY_DIR}/dist/Deepwater-Mark4-${PROJECT_VERSION}-win64.zip
                    WORKING_DIRECTORY ${CMAKE_BINARY_DIR} DEPENDS ${DW_WIN_TARGETS} VERBATIM)
  # The installer: dist/Deepwater-Mark4-<version>-win64.exe, the file the update check (src/app/Updater.cpp) looks for.
  # Needs NSIS (makensis on the PATH, or -DDW_MAKENSIS=<makensis.exe>).
  find_program(DW_MAKENSIS makensis)
  if(DW_MAKENSIS)
    file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/_CPack_Packages/win64/ZIP/Deepwater-Mark4" DW_NSIS_STAGE)
    file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}/windows/Deepwater.ico" DW_NSIS_ICON)
    file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/dist/Deepwater-Mark4-${PROJECT_VERSION}-win64.exe" DW_NSIS_OUT)
    add_custom_target(installer
                      COMMAND ${CMAKE_CPACK_COMMAND} -G ZIP --config ${CMAKE_BINARY_DIR}/CPackConfig.cmake
                      COMMAND ${DW_MAKENSIS} /V2 "/DVERSION=${PROJECT_VERSION}" "/DSTAGE=${DW_NSIS_STAGE}" "/DICON=${DW_NSIS_ICON}"
                              "/DOUTFILE=${DW_NSIS_OUT}" ${CMAKE_SOURCE_DIR}/windows/installer.nsi
                      WORKING_DIRECTORY ${CMAKE_BINARY_DIR} DEPENDS ${DW_WIN_TARGETS} VERBATIM)
  endif()
endif()
