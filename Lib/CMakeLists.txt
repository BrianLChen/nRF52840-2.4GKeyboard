target_sources(app PRIVATE
	src/debounce.c
	src/key_state.c
	src/keyscan_spi.c
	src/rgb.c
	src/keymap.c
	src/kbd_macro.c
	src/kbd_report_pipeline.c
	src/macro_config.c
    src/hid_descriptor.c
    src/hid_report.c
    src/knob.c
)
target_include_directories(app PRIVATE inc)

if(CONFIG_KBD_BLE)
    add_subdirectory(bluetooth)
    # Body's mutually exclusive Gazell/BLE boot modes share the RADIO vector.
    target_sources(app PRIVATE radio/gzll_glue.c)
    target_sources(app PRIVATE radio/gzll_keyboard.c)
    target_include_directories(app PRIVATE radio)
endif()
