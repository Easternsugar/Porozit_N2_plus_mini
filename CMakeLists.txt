# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Disabled minimal build so menuconfig exposes Bluetooth/NimBLE options.
idf_build_set_property(MINIMAL_BUILD OFF)
# Firmware version, reported to the app in the "info" message (PROTOCOL.md)
set(PROJECT_VER "2.0.0")
project(porozit_watch)
