# idf_boot_button_wake_test

ESP-IDF standalone test project to validate USB remote wakeup behavior with `BOOT(GPIO0)`.

## Behavior

- Initializes TinyUSB through `wake_init()`
- Mirrors `main.c`-equivalent suspend/resume tracking via `wake_core` callbacks
- On BOOT press, attempts `tud_remote_wakeup()` with the same gating/log policy:
  - tries only when `mounted && suspended`
  - even when `remote_wakeup_allowed=0`, still attempts and logs it
- Periodically logs USB state and counters

## Build / Flash

```bash
. ~/esp/esp-idf/export.sh
cd /Users/ikunolab/Documents/Arduino/libraries/esp32-wakeup-sleep/idf_boot_button_wake_test
idf.py set-target esp32s3
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

Use CH343P side for stable flashing, and OTG side for TinyUSB remote wake verification.
