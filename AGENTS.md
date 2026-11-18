# AGENTS.md — ZMK config for Beekeeb Toucan

## Repo overview

ZMK firmware config for the [Beekeeb Toucan](https://beekeeb.com/toucan-keyboard/) — wireless
split 42-key column-stagger keyboard with display and trackpad (Cirque Pinnacle on
Toucan 1, Azoteq TPS43 on Toucan 2).

| Branch | Purpose |
|---|---|
| `main` | Normal use (left + right halves, nice_view_gem display) |
| `prospector-dongle` | Keyboard + Prospector ZMK dongle, normal mode |
| `prospector-scanner` | Keyboard + Prospector ZMK dongle, scanner mode |
| `invert-y-scroll` | Y-axis scroll inversion experiment |
| `tap-layer` | Tap layer feature experiment |
| `zmk_4` | ZMK 0.4 pre-release (zmk@main, Zephyr 4.1) + Prospector `feat/new-status-screens` — **do NOT merge to main until ZMK v0.4 is released** |
| `toucan2_zmk4` | `zmk_4` + Toucan 2 upgrade kit (Azoteq TPS43 trackpad replaces the Cirque) — **current working branch** |
| `toucan2_zmk4_bt60` | `toucan2_zmk4` + Polarity Works BT60 as a third split peripheral |
| `toucan2_zmk4_tofu65` | `toucan2_zmk4_bt60` with the BT60 replaced by a KBDfans Tofu65 3.0 Dual-Mode PCB |

## `zmk_4` branch specifics

Based on `prospector-dongle`, migrated to ZMK `main` (Zephyr 4.1, LVGL 9.3):

- **Board renamed**: `seeeduino_xiao_ble` → `xiao_ble//zmk` (Zephyr HWMv2 + ZMK variant).
- **Cirque trackpad** (superseded on `toucan2_zmk4`, see below): used the native Zephyr 4.1
  `cirque,pinnacle` driver; the `cirque-input-module` was removed from `west.yml`.
  Devicetree props renamed (`dr-gpios` → `data-ready-gpios`, taps opt-in via
  `primary-tap-enable`, `x-invert` → `invert-x`). Power saving via `sleep-mode-enable`.
- **Local builds**: `./build.sh [right|left|dongle|reset ...] [--update]` wraps the podman
  invocation — builds the requested targets (default: right left dongle reset; the
  dongle builds all four prospector status-screen layouts) and copies the
  `.uf2`s to `firmware/`. Pass `--update` only after changing `config/west.yml`.
  Uses workspace `~/.cache/zmk-ws-main` and image
  `docker.io/zmkfirmware/zmk-build-arm:4.1-branch` (keep `~/.cache/zmk-ws` +
  `:stable` for v0.3 branches), with `-b "xiao_ble//zmk"`.
  Run `west zephyr-export` in **every** container invocation before `west build` — it
  registers Zephyr's CMake package in the container's ephemeral home, so it doesn't
  persist between runs (failure mode: `Could not find a package configuration file
  provided by "Zephyr"`). If `west update` hits flaky `HTTP/2 stream` fetch errors,
  `git config --global http.version HTTP/1.1` inside the container and retry; it resumes.
- **Pinning policy**: `west.yml` floats on `main` while 0.4 is in flux. If upstream
  breaks, temporarily pin `zmk` to a known-good SHA. When v0.4 is tagged: re-pin all
  modules to `v0.4`, switch the workflow to `@v0.4`, then consider merging.

## `toucan2_zmk4` branch — Toucan 2 trackpad

Cut from `zmk_4`. Swaps the Cirque Pinnacle for the [Toucan 2 upgrade
kit](https://docs.beekeeb.com/toucan2-keyboard/convert-toucan-to-toucan2)'s **Azoteq TPS43**,
porting the sensor and gesture layer from
[`beekeeb/zmk-keyboard-toucan2`](https://github.com/beekeeb/zmk-keyboard-toucan2) onto this
branch's dongle topology. Upstream toucan2 is ZMK v0.3 with the **left half** as central and
no dongle; here the **prospector dongle** is central, so the input listener lives in
`toucan_dongle.overlay`, not on a keyboard half.

### Wiring — a pin-for-pin reuse

The converter breakout board reuses the same 7-pin cable, so the pins map straight across:

| Pin | Toucan 1 (Cirque, SPI0) | Toucan 2 (TPS43, I2C0 @ `0x74`) |
|---|---|---|
| P1.13 | `SPIM_SCK` | `TWIM_SDA` |
| P1.15 | `SPIM_MOSI` | `TWIM_SCL` |
| P1.14 | `SPIM_MISO` | `rst-gpios` |
| P0.02 | `data-ready-gpios` | `rdy-gpios` |
| P0.03 | `cs-gpios` | unused |

Two traps: `xiao_i2c` on `xiao_ble//zmk` is an alias for **`&i2c1`**, not `&i2c0` (so it stays
disabled and we configure `&i2c0` directly); and nRF52840 SPIM0/TWIM0 are the *same*
peripheral instance, so `&spi0` must be explicitly disabled on the right half.

### Modules

`config/west.yml` gains `beekeeb/zmk_driver_azoteq` (the `azoteq,tps43` driver,
`CONFIG_INPUT_TPS43`) and `beekeeb/zmk-input-zoom` (the `zmk,input-processor-zoom`
pinch-to-zoom processor). Neither is tagged upstream, so both track `main`. Last built against
`zmk_driver_azoteq@c329f30` and `zmk-input-zoom@91bbe0c`; pin to a SHA if upstream breaks.

Neither module had been built against ZMK 0.4 before this branch. Their ZMK surface is
small — the driver only touches `zmk/event_manager.h` + `zmk/events/activity_state_changed.h`
(in `tps43_idle_sleeper.c`), and the zoom processor only `zmk_behavior_invoke_binding` plus
`struct zmk_input_processor_state`.

### `swipes` vs `three-finger-swipe` — do not confuse them

Two independent properties, both emitting `INPUT_BTN_NORTH/SOUTH/EAST/WEST`:

- **`swipes`** — the **single-finger** hardware swipe gesture. Leave it **off**: with
  `swipe_button_mapper` in the processor chain it would fire desktop-switch shortcuts
  during ordinary cursor movement.
- **`three-finger-swipe`** — the 3-finger case, derived in software from raw movement
  deltas on *every* report while three fingers are down (not a one-shot hardware gesture),
  so `three-finger-swipe-throttle-ms` (1200 here) is what stops one physical swipe from
  actuating repeatedly. See `tps43_handle_swipe` in `tps43.c`.

Both were added to the driver in `1d5a7e4` / `c329f30`; older revisions only have `swipes`.

### OS mode is a runtime toggle, not a build flag

Upstream gates the zoom/swipe shortcuts behind `#ifdef TOUCAN_WIN_MODE`. Here they are
selected at runtime instead, using `zmk,input-listener` layer overrides. `input_listener.c`
(`app/src/pointing/`, `filter_with_input_config`) scans overrides in **devicetree child
order** and the first one whose layer is active wins, since `process-next` is unset:

| Listener child | `layers` | Effect |
|---|---|---|
| `scroller` (first) | `<1 2>` | one-finger XY→scroll while LOWER/RAISE held |
| `win_mode` (second) | `<5>` | Ctrl-based zoom, Win-key swipes |
| base list | — | Cmd-based zoom, Mission Control swipes (macOS) |

Child order is load-bearing: `scroller` must stay first so a held LOWER/RAISE beats a
toggled-on WIN layer. Toggle with `&tog WIN` on the ADJ layer (row 0, right of `C_VOL_UP`).
ZMK does not persist toggled layers, so it comes back up in macOS mode after a power cycle;
prospector showing `WIN` as the active layer doubles as the OS indicator.

### ZMK bug: layer overrides swallow `ZMK_INPUT_PROC_STOP`

`filter_with_input_config` in `app/src/pointing/input_listener.c`:

```c
int ret = apply_config(cfg->listener_index, &override->config, override_data, data, evt);
if (ret < 0) { return ret; }
if (!override->process_next) { return 0; }   // <-- ret is dropped; 0 == CONTINUE
```

The base path ends with `return apply_config(...)` and propagates the return value, but a
**layer override discards it**. So any processor that consumes an event by returning
`ZMK_INPUT_PROC_STOP` — `zmk,input-processor-behaviors` does this on every code it matches —
works in the base list and silently fails inside a `layers = <...>` child.

This bit us via `INPUT_BTN_TOUCH`. The TPS43 reports it as a **level**: 1 while any finger
is down, 0 when all lift (`tps43.c`, `is_touching = (num_fingers > 0)`), unlike the Cirque,
whose `primary-tap-enable` pulsed it per tap. `handle_key_code` maps `INPUT_BTN_TOUCH`
directly onto mouse button 0, so a leaked event holds the left button for the whole touch —
the pointer drag-selects text.

**Workaround:** `btn_touch_relabel`, a `zmk,input-processor-code-mapper` that rewrites
`INPUT_BTN_TOUCH` → `INPUT_BTN_5`, placed **first in every processor list** on
`trackpad_listener`. Mutating `evt->code` survives regardless of what happens to the return
value, and `INPUT_BTN_5` is not a code `handle_key_code` acts on (it handles `BTN_0..BTN_4`
and `BTN_TOUCH` only), so it is inert. `is_touching_processor` then matches `INPUT_BTN_5`.

Two consequences worth remembering:

- **Any** new layer override on this listener must also start with `&btn_touch_relabel`.
- `is_touching_processor` is repeated in every list, including `scroller`, so a touch that
  begins in one mode and ends in another still gets its `&mo 4` release — otherwise the MOU
  layer sticks on.

The other consumers are safe by luck: `swipe_button_mapper` matches
`BTN_NORTH/EAST/SOUTH/WEST` (`0x130`–`0x134`), none of which `handle_key_code` acts on.

### Keymap layers

`0 BASE, 1 LOWER, 2 RAISE, 3 ADJ, 4 MOU, 5 WIN` (`extra_3` still reserved; dropped on
`toucan2_zmk4_bt60`, see below). MOU is held
automatically by `is_touching_processor` while a finger rests on the pad, turning the six
thumb keys into mouse buttons. WIN is fully transparent — it exists only as a flag for the
listener override, so MOU's bindings still resolve underneath it.

## `toucan2_zmk4_bt60` branch — BT60 as a third peripheral

Cut from `toucan2_zmk4`. A Polarity Works BT60 V2 (upstream ZMK board `bt60//zmk`, rev
2.0.0) joins the two Toucan halves as split peripheral #3 of the prospector dongle, via
the `toucan_bt60` shield (`boards/shields/toucan_bt60/`).

- **One position space.** `toucan-bt60-layout.dtsi` defines `toucan_bt60_transform` /
  `toucan_bt60_layout`: positions 0–41 are the Toucan (same order as `default_transform`,
  so the halves are unchanged and need no reflash), 42–106 the BT60 (ISO top rows,
  all-1U bottom row). The dongle uses it as its only physical layout; the BT60 uses it
  with `row-offset = <4>`, so its kscan rows 1–5 report as positions 42–106. Split
  position bitmaps are 16 bytes, so positions must stay below 128.
- **Layout index trap.** The central pushes its selected physical layout to peripherals
  as an *index* (`split_svc_select_phys_layout` → `zmk_physical_layouts_select`), and the
  dongle always sends 0. The `bt60` board brings four `layout_60_*` layouts; while they're
  enabled, index 0 is `layout_60_all1u`, so its transform silently replaces ours and every
  key lands on the wrong position (`1`→q, space→r). `toucan_bt60.overlay` disables them so
  `toucan_bt60_layout` is the only layout. A `chosen` physical layout is not enough.
- **Keymap.** Every layer in `config/toucan.keymap` has 107 bindings. The BT60 shield
  resolves to the same file (keymap candidate `toucan`), which keeps the board's
  `bt60_2_0_0.keymap` out. BT60 has no `sensor-bindings` (no encoder) and no lighting
  (`toucan_bt60.conf` disables underglow/backlight/ext-power; `&rgb_ug` can't run on the
  LED-less dongle). The BT60's own Fn and system layers live on the BT60 positions of
  RAISE and LOWER. The key right of UP is `&mo RAISE` and the key right of SPACE is
  `&mo LOWER`, so holding both reaches ADJ through `tri_layer`, and either one turns the
  trackpad into a scroller (`scroller`, `layers = <1 2>`).
- **Dongle RAM is the constraint.** The dongle was at 94–97% RAM before this branch
  (radii highest), and `zmk_keymap` is layers × positions × 16 B; the BT60 adds ~12 KB.
  To fit: `extra_3` was dropped (no Studio add-layer slot), `BT_MAX_CONN`/`BT_MAX_PAIRED`
  are 7 = 3 peripherals + 4 host profiles, and `config/toucan_dongle.conf` sets
  `CONFIG_LV_Z_VDB_SIZE=40` (prospector default 50; frees ~26 KB of LVGL draw buffers).
  Each extra layer costs 1,712 B.
- **USB on the BT60 only charges it.** `ZMK_USB` is central-only, so with a cable it still
  types through the dongle. The PCB switch is a battery power switch, not a mode switch.
- **First pairing:** flash `settings_reset_bt60` and then `toucan_bt60` to the BT60. A
  peripheral with an old host bond does directed advertising and never finds the dongle.
  Local builds: `./build.sh bt60 bt60_reset`.

## `toucan2_zmk4_tofu65` branch — Tofu65 3.0 replaces the BT60

Cut from `toucan2_zmk4_bt60`. The BT60 is gone; a KBDfans Tofu65 3.0 **Dual-Mode** PCB
(shield `toucan_tofu65`, `boards/shields/toucan_tofu65/`) is split peripheral #3.

- **Not upstream `kbdfans/tofu65`.** That ZMK board is the Tofu65 **2.0** (RP2040, wired).
  The 3.0 dual-mode PCB is the community nRF52840 board **`klink`**, ported here to HWMv2 as
  `klink//zmk` (`boards/kbdfans/klink/`) from
  [yangdigi/zmk-keyboards-klink](https://github.com/yangdigi/zmk-keyboards-klink) (HWMv1,
  unloadable on Zephyr 4.1). Hardware facts come from
  [openkbd/zmk-config-tofu65-ble-3.0](https://github.com/openkbd/zmk-config-tofu65-ble-3.0).
- **Board quirks:** no 32 kHz crystal (`CLOCK_CONTROL_NRF_K32SRC_RC`), DCDC on, row 4 on NFC
  pin P0.09 (`&uicr { nfct-pins-as-gpios; }`), battery on AIN3, and the bootloader only
  accepts UF2 family **`0x59952840`** (`BUILD_OUTPUT_UF2_FAMILY_ID` in the board defconfig).
- **Columns are a 74HC595** (`zmk,gpio-595` on `&spi0`: SCK P1.13, MOSI P0.28, CS P1.00).
  Deep sleep (`ZMK_SLEEP`, same timeouts as the halves) wakes via `wakeup-source` on
  `kscan0`: the 595 has no PM hook, so the all-active column outputs stay latched in
  System OFF. The physical power switch is under Caps Lock.
- **Position space:** `toucan-tofu65-layout.dtsi` (109 positions, 15 × 9): 0–41 Toucan,
  42–108 Tofu65 (ANSI, 2U backspace), `row-offset = <4>`. `klink` has no layouts of its own,
  so the layout-index trap from the BT60 doesn't apply.
- **Keymap:** Tofu65 right column is menu=Home, raise=`&lt RAISE PG_UP`,
  lower=`&lt LOWER PG_DN`, func=End. BT60 Fn/system keys moved to the same spots on RAISE
  and LOWER.
- **First pairing / switching from the BT60:** the dongle's three peripheral slots still
  hold the BT60, so flash `settings_reset` to dongle + both halves and
  `settings_reset_tofu65` to the Tofu65, then the firmware. Local builds:
  `./build.sh tofu65 tofu65_reset`.

## How CI builds

`.github/workflows/build.yml` delegates to the ZMK reusable workflow:

```yaml
uses: zmkfirmware/zmk/.github/workflows/build-user-config.yml@v0.3
```

- Image: `zmkfirmware/zmk-build-arm:stable`
- Matrix: defined by `build.yaml` at the repo root (one entry per firmware artifact)
- Because `zephyr/module.yml` exists, CI copies `config/` into an isolated temp dir and adds
  the repo root as `-DZMK_EXTRA_MODULES` — your in-repo shields and modules are included automatically.

## Build targets by branch

**`main`**
```yaml
- board: seeeduino_xiao_ble
  shield: toucan_left rgbled_adapter nice_view_gem
  snippet: studio-rpc-usb-uart
  cmake-args: -DCONFIG_ZMK_STUDIO=y
- board: seeeduino_xiao_ble
  shield: toucan_right rgbled_adapter
- board: seeeduino_xiao_ble
  shield: settings_reset
```

**`prospector-dongle`**
```yaml
- board: seeeduino_xiao_ble
  shield: toucan_dongle rgbled_adapter prospector_adapter
  snippet: studio-rpc-usb-uart
  cmake-args: -DCONFIG_ZMK_STUDIO=y
- board: seeeduino_xiao_ble
  shield: toucan_left rgbled_adapter toucan_pet
- board: seeeduino_xiao_ble
  shield: toucan_right rgbled_adapter
- board: seeeduino_xiao_ble
  shield: settings_reset
```

## Local builds with Podman

Local builds use the same container image as CI — fast iteration without waiting for GitHub Actions.

### One-time setup
```bash
sudo pacman -S --needed podman           # Arch; adapt for your distro
podman pull docker.io/zmkfirmware/zmk-build-arm:stable
mkdir -p ~/.cache/zmk-ws                 # persistent workspace; zmk/zephyr cloned once here
```

### Build a target

Run from the repo root, with the branch you want to test checked out:

```bash
podman run --rm \
  -v "$PWD":/work:ro \
  -v "$HOME/.cache/zmk-ws":/ws \
  docker.io/zmkfirmware/zmk-build-arm:stable \
  bash -lc '
    set -e
    cd /ws
    rm -rf config && mkdir config && cp -R /work/config/* config/
    [ -d .west ] || west init -l /ws/config
    west update --fetch-opt=--filter=tree:0
    west zephyr-export
    west build -p -s zmk/app -d build/right -b seeeduino_xiao_ble -- \
      -DZMK_CONFIG=/ws/config \
      -DSHIELD="toucan_right rgbled_adapter" \
      -DZMK_EXTRA_MODULES=/work
  '
```

- First run clones ZMK + Zephyr into `~/.cache/zmk-ws` (~1 GB, ~5–10 min).
- Subsequent builds reuse the workspace and take ~1–2 min per target.
- Output: `~/.cache/zmk-ws/build/<name>/zephyr/zmk.uf2`

**Adapting for other targets** — change `-d build/<name>`, `-DSHIELD`, and add snippet/cmake-args as needed:

```bash
# toucan_left + nice_view_gem (main branch, with ZMK Studio)
west build -p -s zmk/app -d build/left -b seeeduino_xiao_ble \
  -S studio-rpc-usb-uart -- \
  -DZMK_CONFIG=/ws/config \
  -DSHIELD="toucan_left rgbled_adapter nice_view_gem" \
  -DZMK_EXTRA_MODULES=/work \
  -DCONFIG_ZMK_STUDIO=y

# toucan_dongle (prospector-dongle branch, with ZMK Studio)
west build -p -s zmk/app -d build/dongle -b seeeduino_xiao_ble \
  -S studio-rpc-usb-uart -- \
  -DZMK_CONFIG=/ws/config \
  -DSHIELD="toucan_dongle rgbled_adapter prospector_adapter" \
  -DZMK_EXTRA_MODULES=/work \
  -DCONFIG_ZMK_STUDIO=y
```

## Confirming with CI

```bash
# Trigger a run manually
gh workflow run build.yml --ref <branch>

# Watch the run
gh run list --branch <branch> --limit 3
gh run watch <RUN_ID>

# Get raw job logs (most useful for compiler errors)
gh run view <RUN_ID> --json jobs \
  --jq '.jobs[] | "\(.databaseId) \(.name) \(.conclusion)"'
gh api repos/mbarrben/zmk-keyboard-toucan/actions/jobs/<JOB_ID>/logs
```

The reusable workflow sets `fail-fast: false`, so all matrix jobs run even if one fails — check
every failing job, not just the first.

## Dependency management (`config/west.yml`)

All ZMK modules are pinned here. Rules learned from experience:

- **Pin third-party modules to explicit version tags** (e.g. `revision: v0.3`), not `main`.
  Modules tracking `main` can silently diverge from the ZMK version in use and break builds.
- **ZMK version** is set by `defaults.revision: v0.3` and applied to the `zmk` project via
  `import: app/west.yml`. Bumping ZMK requires validating all module revisions too.
- If a build dies in a module's source before reaching your shield code, the root cause is
  almost always a module revision mismatch — check `west.yml` first.

## Common pitfalls

**`zmk-rgbled-widget@main` vs ZMK v0.3**
The `@main` revision of `zmk-rgbled-widget` (caksoylar) has diverged from ZMK v0.3. It causes
a static assertion failure at compile time:
```
gcc.h:87:36: error: static assertion failed: "An alias for a red LED is not found for RGBLED_WIDGET"
```
Fix: pin to `revision: v0.3` in `west.yml`. The `rgbled_adapter` shield bundled at v0.3 supplies
the required LED devicetree aliases.

**Build hides downstream errors**
If all builds that include `rgbled_adapter` fail at the same place, downstream shields
(`prospector_adapter`, `toucan_pet`, etc.) haven't been compiled yet — their errors are hidden.
Fix the rgbled error first, then rebuild to reveal any next layer of failures.

**`gh run view --job <id> --log` is unreliable** — returns only the first line.
Use `gh api repos/mbarrben/zmk-keyboard-toucan/actions/jobs/<JOB_ID>/logs` instead (plain text,
full log, requires `repo` scope on gh token).
