# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hid-keyboard)

option(KBD_ENABLE_LOG "Enable KBD firmware logs" OFF)
option(KBD_ENABLE_SCAN_TRACE "Log raw and normalized scan bytes every 250 ms" OFF)
if(KBD_ENABLE_LOG OR KBD_ENABLE_SCAN_TRACE)
	target_compile_definitions(app PRIVATE KBD_ENABLE_LOG)
endif()
if(KBD_ENABLE_SCAN_TRACE)
	target_compile_definitions(app PRIVATE KBD_ENABLE_SCAN_TRACE)
endif()

include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
zephyr_include_directories(src)
add_subdirectory(../Lib Lib_build)
target_sources(app PRIVATE
	src/main.c
)
