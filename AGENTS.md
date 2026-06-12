# AGENTS.md — ZMK config for Beekeeb Toucan

## Repo overview

ZMK firmware config for the [Beekeeb Toucan](https://beekeeb.com/toucan-keyboard/) — wireless
split 42-key column-stagger keyboard with display and Cirque trackpad.

| Branch | Purpose |
|---|---|
| `main` | Normal use (left + right halves, nice_view_gem display) |
| `prospector-dongle` | Keyboard + Prospector ZMK dongle, normal mode |
| `prospector-scanner` | Keyboard + Prospector ZMK dongle, scanner mode |
| `invert-y-scroll` | Y-axis scroll inversion experiment |
| `tap-layer` | Tap layer feature experiment |
| `zmk_4` | ZMK 0.4 pre-release (zmk@main, Zephyr 4.1) + Prospector `feat/new-status-screens` — **do NOT merge to main until ZMK v0.4 is released** |

## `zmk_4` branch specifics

Based on `prospector-dongle`, migrated to ZMK `main` (Zephyr 4.1, LVGL 9.3):

- **Board renamed**: `seeeduino_xiao_ble` → `xiao_ble//zmk` (Zephyr HWMv2 + ZMK variant).
- **Cirque trackpad**: uses the native Zephyr 4.1 `cirque,pinnacle` driver; the
  `cirque-input-module` was removed from `west.yml`. Devicetree props renamed
  (`dr-gpios` → `data-ready-gpios`, taps now opt-in via `primary-tap-enable`,
  `x-invert` → `invert-x`). Trackpad power saving via `sleep-mode-enable` (hardware,
  5 s idle / ~300 ms wake) — the old module's ZMK-idle sleep hack has no native equivalent.
- **Local builds**: separate workspace `~/.cache/zmk-ws-main` and image
  `docker.io/zmkfirmware/zmk-build-arm:4.1-branch` (keep `~/.cache/zmk-ws` +
  `:stable` for v0.3 branches). Same podman invocation otherwise, with `-b "xiao_ble//zmk"`.
- **Pinning policy**: `west.yml` floats on `main` while 0.4 is in flux. If upstream
  breaks, temporarily pin `zmk` to a known-good SHA. When v0.4 is tagged: re-pin all
  modules to `v0.4`, switch the workflow to `@v0.4`, then consider merging.

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
