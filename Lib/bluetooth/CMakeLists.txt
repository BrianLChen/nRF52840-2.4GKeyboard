target_sources(app PRIVATE ble_keyboard.c pairing.c pairing_store.c)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# Both settings write entry points must be intercepted; LTO must stay disabled.
zephyr_ld_options(-Wl,--wrap=settings_save_one -Wl,--wrap=settings_delete)
