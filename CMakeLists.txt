cmake_minimum_required(VERSION 3.16.0)

# Pre-build: generate .inc files by running npm scripts before compilation
set(CMAKE_SCRIPTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/scripts")

# Get npm path if NPM executable is not in PATH
if(NOT COMMAND npm)
    find_program(NPM_EXECUTABLE "npm" PATHS /usr/local/opt/node* PATH_SUFFIXES/bin)
endif()

if(NOT DEFINED NPM_EXECUTABLE)
    message(FATAL_ERROR "[pre_build] npm not found. Please install Node.js/npm.")
endif()

# Step 1: npm install (fetch dependencies)
message(STATUS "[pre_build] Running npm install...")
execute_process(
    COMMAND ${NPM_EXECUTABLE} "ci" "--prefer-offline"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE NPM_INSTALL_RET
    OUTPUT_QUIET
    ERROR_QUIET
)

if(NOT NPM_INSTALL_RET EQUAL 0)
    message(FATAL_ERROR "[pre_build] npm install failed")
endif()

# Step 2: npm build (generate web assets in ./dist)
message(STATUS "[pre_build] Running npm run build...")
execute_process(
    COMMAND ${NPM_EXECUTABLE} "run" "build"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE NPM_BUILD_RET
    OUTPUT_QUIET
    ERROR_QUIET
)

if(NOT NPM_BUILD_RET EQUAL 0)
    message(FATAL_ERROR "[pre_build] npm build failed")
endif()

# Step 3: Generate version header
message(STATUS "[pre_build] Running gen_version.py...")
execute_process(
    COMMAND python3 "${CMAKE_SCRIPTS_DIR}/gen_version.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE VER_RET
    ERROR_QUIET OUTPUT_QUIET
)

# Step 4: Embed assets into inc/ headers
message(STATUS "[pre_build] Running embed_assets.py...")
execute_process(
    COMMAND python3 "${CMAKE_SCRIPTS_DIR}/embed_assets.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE EMBED_RET
    ERROR_QUIET OUTPUT_QUIET
)

if(NOT EMBED_RET EQUAL 0)
    message(FATAL_ERROR "[pre_build] embed_assets failed")
endif()

message(STATUS "[pre_build] ✅ Web assets embedded successfully to inc/")

# After npm build completes, track the generated dist/* files for incremental rebuilds
# First, create a manifest file listing all web/dist source files
add_custom_target(generate_asset_manifest
    COMMAND find "${CMAKE_CURRENT_SOURCE_DIR}/dist" -type f | sort > "${CMAKE_BINARY_DIR}/web_asset_files.txt"
    COMMENT "[pre_build] Generating web asset file list..."
)

# Use the manifest as dependency to embed_assets.py
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/.embed_complete"
    DEPENDS 
        "${CMAKE_SCRIPTS_DIR}/embed_assets.py"
        "${CMAKE_BINARY_DIR}/web_asset_files.txt"
    COMMAND python3 "${CMAKE_SCRIPTS_DIR}/embed_assets.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "[pre_build] Embedding web assets (checks changes in dist/ folder)"
)

add_custom_target(pre_build_embed ASSETS "${CMAKE_BINARY_DIR}/.embed_complete")
add_dependencies(${PROJECT_NAME} pre_build_embed)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(DeepGlow)
set(PROJECT_VERSION "1.0.4" CACHE STRING "Firmware version")
