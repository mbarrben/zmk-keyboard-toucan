# ZMK config for beekeeb Toucan Keyboard

[The beekeeb Toucan Keyboard](https://beekeeb.com/toucan-keyboard/) is a wireless split 42-key column‑stagger keyboard that a display and a trackpad, with an aggressive stagger on the pinky columns.

# KBDfans Tofu65 3.0 peripheral

On this branch a [KBDfans Tofu65 3.0](https://kbdfans.com/products/tofu65-3-0) (Dual-Mode PCB) joins the two Toucan halves as a third split peripheral of the prospector dongle.

The Tofu65 support is a port of existing ZMK v0.3 configs to ZMK main / Zephyr 4.1 (hardware model v2). It is not new work:

- [yangdigi/zmk-keyboards-klink](https://github.com/yangdigi/zmk-keyboards-klink): the `klink` nRF52840 board definition used by the Tofu65 3.0 Dual-Mode PCB. `boards/kbdfans/klink/` is a port of it.
- [openkbd/zmk-config-tofu65-ble-3.0](https://github.com/openkbd/zmk-config-tofu65-ble-3.0): the Tofu65 3.0 ZMK config (74HC595 column shifter, row pins, matrix transform, ANSI layout, battery divider, UF2 family ID). `boards/shields/toucan_tofu65/` takes its hardware definitions from here. A fork is at [faytahl/zmk-config-tofu65-ble-3.0](https://github.com/faytahl/zmk-config-tofu65-ble-3.0).

## Entering the Tofu65 bootloader

With this firmware running, hold raise + Enter. With KBDfans' stock firmware, hold Fn + B for 3 seconds.

If no key combo works (for example, after flashing `settings_reset_tofu65`, which has no keymap), use the bootloader's own entry:

1. Move the power switch (under the Caps Lock keycap) to the off position.
2. Hold Esc (the top-left key).
3. Plug in the USB cable. The UF2 drive appears; release Esc and copy the `.uf2` onto it.

If Windows reports an unknown USB device (Code 43) instead, plug the cable straight into the PC rather than through a hub.

# License

The code in this repo is available under the MIT license.

The included shield nice_view_gem is modified from https://github.com/M165437/nice-view-gem licensed under the MIT License.

The board `klink` is ported from https://github.com/yangdigi/zmk-keyboards-klink, licensed under the MIT License.

ZMK code snippets are taken from the ZMK documentation under the MIT license.

The embedded font QuinqueFive is designed by GGBotNet, licensed under under the SIL Open Font License, Version 1.1.
