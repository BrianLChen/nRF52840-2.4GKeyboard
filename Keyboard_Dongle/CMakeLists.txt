# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hid-keyboard)

option(KBD_ENABLE_LOG "Enable KBD firmware logs" OFF)
if(KBD_ENABLE_LOG)
	target_compile_definitions(app PRIVATE KBD_ENABLE_LOG)
endif()

include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
zephyr_include_directories(../Lib/inc)
target_sources(app PRIVATE
	../Lib/src/hid_descriptor.c
	src/main.c
)
