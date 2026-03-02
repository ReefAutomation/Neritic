cmake_minimum_required(VERSION 3.16.0)

if(ENABLE_NATIVE_HOMEKIT_COMPONENTS)
	set(EXTRA_COMPONENT_DIRS
		${CMAKE_SOURCE_DIR}/src
		${CMAKE_SOURCE_DIR}/external/libsodium
		${CMAKE_SOURCE_DIR}/external/esp-protocols/components/mdns
		${CMAKE_SOURCE_DIR}/external/esp-homekit-sdk/components/homekit/esp_hap_core
		${CMAKE_SOURCE_DIR}/external/esp-homekit-sdk/components/homekit/esp_hap_platform
		${CMAKE_SOURCE_DIR}/external/esp-homekit-sdk/components/homekit/esp_hap_apple_profiles
		${CMAKE_SOURCE_DIR}/external/esp-homekit-sdk/components/homekit/esp_hap_extras
		${CMAKE_SOURCE_DIR}/external/esp-homekit-sdk/components/homekit/hkdf-sha
		${CMAKE_SOURCE_DIR}/external/esp-homekit-sdk/components/homekit/mu_srp
		${CMAKE_SOURCE_DIR}/external/esp-homekit-sdk/components/homekit/json_generator
		${CMAKE_SOURCE_DIR}/external/esp-homekit-sdk/components/homekit/json_parser
	)
	list(FILTER EXTRA_COMPONENT_DIRS EXCLUDE REGEX "^$")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(DeepGlow)
