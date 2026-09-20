# Session Note

## Scope Rules

- `Lib/` is shared by `Keyboard_Body` and `Keyboard_Dongle`.
- Do not modify `Lib/` from the dongle-side work.
- Any desired `Lib/` changes should be recorded here for the Body-side maintainer to review and apply.

## Current Structure Observed

- Top-level shared library and shared defines:
  - `Lib/CMakeLists.txt`
  - `Lib/inc/kbd_define.h`
  - `Lib/inc/keymap.h`
  - `Lib/src/keymap.c`
  - other shared `Lib/inc` and `Lib/src` files
- Body app:
  - `Keyboard_Body/src/main.c`
  - `Keyboard_Body/CMakeLists.txt`
  - Body already uses shared `../Lib` through `add_subdirectory(../Lib Lib_build)`.
- Dongle app:
  - `Keyboard_Dongle/src/main.c`
  - `Keyboard_Dongle/CMakeLists.txt`
  - Dongle should use shared `../Lib/inc/kbd_define.h` for the USB report contract.

## Dongle-Side Change Made

- Updated `Keyboard_Dongle/CMakeLists.txt` so dongle includes shared `../Lib/inc` instead of local `src`.
- This prevents `Keyboard_Dongle/src/kbd_define.h` from shadowing shared `Lib/inc/kbd_define.h`.

Current intended dongle CMake include line:

```cmake
zephyr_include_directories(../Lib/inc)
```

## Lib Changes Requested

None at this time.

## Resolved Notes

- Shared ACK payload size now exists as `KBD_GZLL_ACK_PAYLOAD_BYTES` in `Lib/inc/kbd_define.h`.
- Dongle keeps `DONGLE_GZLL_ACK_BYTES` as a local alias of `KBD_GZLL_ACK_PAYLOAD_BYTES`.
- `Keyboard_Dongle/app.overlay` intentionally keeps `in-report-size = <64>;`. This is the USB endpoint maximum report size, and actual HID reports may be smaller than the declared maximum.

## Body/Lib Maintainer Notes

- There are stale duplicated body-side files under `Keyboard_Dongle/src/`:
  - `Keyboard_Dongle/src/kbd_define.h`
  - `Keyboard_Dongle/src/keymap.h`
  - `Keyboard_Dongle/src/keymap.c`
- There is also a stale duplicated library tree under `Keyboard_Dongle/Lib/`.
- Dongle CMake no longer uses those duplicates, so they are not part of the active dongle build.
- Recommend removing or archiving the stale duplicates later to avoid confusion, but this was not done here.
