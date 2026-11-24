# macOS package (the `dist` target, cmake -P): Deepwater.app with Qt inside it, signed ad hoc, in a .dmg beside a link to
# /Applications. Called with -DAPP=<built Deepwater.app> -DOUT=<dir> -DVERSION=<x.y.z> -DMACDEPLOYQT=<path>.
#   1. a copy of the bundle (the build's own stays as it is: the next build would otherwise write over a deployed one)
#   2. macdeployqt: Qt's frameworks and plugins into the bundle, the helpers (model runner, translator) relinked too
#   3. an ad hoc signature over the whole bundle (macdeployqt's changes break the build's): enough to run on Apple
#      Silicon; a downloaded copy still meets Gatekeeper's first-launch warning, the in-app update does not
#   4. Deepwater-<version>-macos.dmg (compressed), what the release workflow publishes and the updater installs
foreach(v APP OUT VERSION MACDEPLOYQT)
  if(NOT ${v})
    message(FATAL_ERROR "MacDist.cmake: ${v} is not set")
  endif()
endforeach()
set(STAGE ${OUT}/stage)
set(DMG ${OUT}/Deepwater-${VERSION}-macos.dmg)
file(REMOVE_RECURSE ${STAGE})
file(MAKE_DIRECTORY ${STAGE})
execute_process(COMMAND ditto ${APP} ${STAGE}/Deepwater.app COMMAND_ERROR_IS_FATAL ANY)
set(HELPERS)
foreach(h dw-translate)
  if(EXISTS ${STAGE}/Deepwater.app/Contents/MacOS/${h})
    list(APPEND HELPERS -executable=${STAGE}/Deepwater.app/Contents/MacOS/${h})
  endif()
endforeach()
execute_process(COMMAND ${MACDEPLOYQT} ${STAGE}/Deepwater.app -always-overwrite ${HELPERS} COMMAND_ERROR_IS_FATAL ANY)
# Qt's SQL drivers other than SQLite (the only one Deepwater uses): ODBC, PostgreSQL and Mimer link libraries a Mac
# doesn't have, which macdeployqt reports and the app would fail to load anyway
file(GLOB DW_UNUSED_SQL ${STAGE}/Deepwater.app/Contents/PlugIns/sqldrivers/*)
list(FILTER DW_UNUSED_SQL EXCLUDE REGEX "libqsqlite\\.dylib$")
if(DW_UNUSED_SQL)
  file(REMOVE ${DW_UNUSED_SQL})
endif()
execute_process(COMMAND codesign --force --deep --sign - ${STAGE}/Deepwater.app COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND codesign --verify --deep --strict ${STAGE}/Deepwater.app COMMAND_ERROR_IS_FATAL ANY)
file(CREATE_LINK /Applications ${STAGE}/Applications SYMBOLIC)
file(REMOVE ${DMG})
execute_process(COMMAND hdiutil create -volname Deepwater -srcfolder ${STAGE} -ov -format UDZO ${DMG} COMMAND_ERROR_IS_FATAL ANY)
message(STATUS "macOS package: ${DMG}")
