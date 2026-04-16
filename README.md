# esp32-wakeup-sleep

Minimal USB remote wake core for ESP32 TinyUSB device mode.

do this
```zsh
cd ~/Documents/Arduino/libraries
git clone https://github.com/9mada4/esp32-wakeup-sleep.git 
```

## Public API

- `esp_err_t wake_init(void);`
- `wake_state_t wake_get_state(void);`
- `bool wake_is_ready(void);`
- `bool wake_trigger(void);`

## Layout

- `src/wake_core.c`
- `src/wake_core.h`
- `examples/minimal/minimal.ino`

## ESP-IDF

This repository is structured as a single component. Add it to an ESP-IDF project and include `wake_core.h`.

## Arduino IDE

The repository also uses Arduino library layout via `src/` and `library.properties`, so Arduino IDE can discover it as a library.

Expected constraints:

- Board support must expose TinyUSB device mode.
- The Arduino core must ship the ESP-IDF TinyUSB headers used by this library.
- Remote wakeup only works after the host suspends the USB device and allows remote wakeup.

The included sketch at `examples/minimal/minimal.ino` is the baseline smoke test for Arduino IDE integration.
