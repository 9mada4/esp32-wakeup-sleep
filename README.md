# esp32-wakeup-sleep

Minimal USB remote wake core for ESP32 TinyUSB device mode.

do this
```zsh
cd ~/Documents/Arduino/libraries
git clone https://github.com/9mada4/esp32-wakeup-sleep.git 
```

## Public API

- `esp_err_t wake_init(void);`
- `void wake_usb_on_suspend(bool remote_wakeup_en);`
- `void wake_usb_on_resume(void);`
- `uint32_t wake_usb_get_suspend_seq(void);`
- `wake_state_t wake_get_state(void);`
- `bool wake_trigger(void);`

## Layout

- `src/wake_core.c`
- `src/wake_core.h`
- `src/esp32s3/libwakecore.a` (generated, precompiled archive slot)
- `tools/build_idf_archive.sh`
- `examples/minimal/minimal.ino`

## ESP-IDF

This repository is structured as a single component. Add it to an ESP-IDF project and include `wake_core.h`.

## Arduino IDE (`.a` linkage)

This repository is configured for precompiled archive linkage:

- Build the low-level implementation once with ESP-IDF:
```zsh
./tools/build_idf_archive.sh esp32s3
```
- This generates `src/esp32s3/libwakecore.a`.
- The build script forces `CONFIG_TINYUSB_HID_COUNT=1` in its temporary ESP-IDF project so the HID Mouse class is linked for USB enumeration.
- Arduino IDE then links that archive (`precompiled=full`, `ldflags=-lwakecore`) instead of rebuilding internals.
- If the archive is missing on ESP32-S3, the source path intentionally errors to prevent accidental non-IDF fallback.
- USB suspend/resume state is tracked inside the archive via TinyUSB callbacks (`tud_suspend_cb`/`tud_resume_cb`), so Arduino sketch-side USB event wiring is not required for the basic wake path.

Current support target:

- ESP32-S3 (`build.mcu=esp32s3`)

Constraints:

- Toolchain/ABI must match the Arduino core generation you link against.
- The archive's unresolved IDF symbols must exist in Arduino's link graph (see `src/esp32s3/libwakecore.undefined.txt`).
- Board support must expose TinyUSB device mode.
- On host side, USB must stay in suspend (not power-cut) for remote wake to work.

The included sketch at `examples/minimal/minimal.ino` is the baseline smoke test for Arduino IDE integration.
