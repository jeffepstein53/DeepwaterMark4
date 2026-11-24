# Read by cpack for each package it makes (CPACK_PROJECT_CONFIG_FILE in CMakeLists.txt).
# The .zip (Deepwater Mark 3's published form, 2026-11-24): the folder inside it has no version (Deepwater-Mark3, as in
# 0.3.0), so a new version unzipped where the previous one was replaces its files instead of landing beside them.
# cpack names the archive after that folder (Deepwater-Mark3.zip); the dist target in CMakeLists.txt then renames it
# Deepwater-Mark3-<version>-win64.zip, the name the update check (src/app/Updater.cpp) looks for.
if(CPACK_GENERATOR STREQUAL "ZIP")
  set(CPACK_PACKAGE_FILE_NAME "Deepwater-Mark3")
endif()
