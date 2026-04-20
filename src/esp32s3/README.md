# Precompiled Archive Slot (`esp32s3`)

Place the ESP-IDF-built archive here as:

- `libwakecore.a`

Arduino IDE uses `library.properties` (`precompiled=full`, `ldflags=-lwakecore`) and links this archive for `build.mcu=esp32s3`.

Generate/update it with:

```zsh
./tools/build_idf_archive.sh esp32s3
```

The script also emits unresolved symbol list to:

- `libwakecore.undefined.txt`
