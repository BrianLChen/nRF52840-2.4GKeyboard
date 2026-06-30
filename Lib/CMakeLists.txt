target_sources(app PRIVATE
	src/debounce.c
	src/key_state.c
	src/keyscan_spi.c
	src/rgb.c
	src/keymap.c
	src/hid_descriptor.c
)
target_include_directories(app PRIVATE inc)
