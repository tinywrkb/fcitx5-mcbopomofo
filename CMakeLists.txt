cmake_minimum_required(VERSION 3.6)
project(fcitx5-mcbopomofo VERSION 2.2.1)

find_package(ECM REQUIRED 1.0.0)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

find_package(Fcitx5Core REQUIRED)
include(FeatureSummary)
include(GNUInstallDirs)
include(ECMSetupVersion)
include(ECMUninstallTarget)

find_package(fmt REQUIRED)
find_package(Gettext REQUIRED)

# Setup some compiler option that is generally useful and compatible with Fcitx 5 (C++17)
include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")

# So that `make install` does not also install Google Test
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

add_subdirectory(src)
add_subdirectory(data)
add_subdirectory(po)

message(STATUS "CMAKE_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX}")
message(STATUS "CMAKE_INSTALL_DATADIR ${CMAKE_INSTALL_DATADIR}")

# McBopomofo data
configure_file(data/data.txt mcbopomofo-data.txt)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/mcbopomofo-data.txt" DESTINATION "${FCITX_INSTALL_PKGDATADIR}/data")
