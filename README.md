# hoja-dongle-fw

## Build targets

The repository provides CMake presets for the HOJA dongle and the official
Raspberry Pi wireless Pico boards:

```sh
cmake --preset hoja
cmake --build --preset hoja

cmake --preset pico-w
cmake --build --preset pico-w

cmake --preset pico-2w
cmake --build --preset pico-2w
```

Build all three variants with one CMake invocation:

```sh
cmake -P cmake/build_all.cmake
```

The `pico-w` and `pico-2w` configurations use the SDK's standard board pinouts
and support USB controller modes only. N64 and GameCube wired Joybus modes and
the HOJA board's status LEDs/buttons are disabled in those builds.

The equivalent low-level configuration option is
`-DHOJA_DONGLE_BOARD=<hoja|pico_w|pico2_w>`.
