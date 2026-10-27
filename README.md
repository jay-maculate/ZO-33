# ZO-33

Firmware for the sample-slot device. Headphones and USB audio are 16-bit stereo at 44.1 kHz. Pad samples, recordings, and resamples are 16-bit mono at 44.1 kHz.

## Build

Install the ARM embedded toolchain (`arm-none-eabi-gcc`) and CMake, then:

```
make zeptocore
```

The first build clones the Pico SDK and Pico Extras. The flashable file is `zeptocore.uf2`. Copy it to the board's USB drive while the device is in bootloader mode.

## Card preparation

`tools/prepare-card/index.html` writes the reserved sample slots. Serve that directory over HTTP and open it in Chrome or Edge. A `file://` page cannot write the card.

## Display

`display/index.html` is the browser view of the current project. Open it in Chrome or Edge, click Connect, and choose the device's USB serial port. The page draws the selected sample from peaks the device sends, plus the project, pattern, filter, and playback position. The hosted page is `https://jay-maculate.github.io/ZO-33/display/`.

## Layout

Each bank stores its patterns, tempo, and volume in `project.bin`. A bank with no project file starts at 90 BPM.
