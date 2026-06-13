cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/.pio/.version" RAW_VERSION)
set(PROJECT_VER "${RAW_VERSION}")

project(DeepGlow)
