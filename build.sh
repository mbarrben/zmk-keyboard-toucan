#!/usr/bin/env bash
# Build ZMK firmware locally with podman or docker (zmk_4 branch: ZMK main / Zephyr 4.1).
#
# Usage:
#   ./build.sh [targets...] [--update]
#
#   targets   any of: right left dongle reset bt60 bt60_reset (default: all)
#   --update  run `west update` first — needed only after changing config/west.yml
#
# Output: firmware/<target>.uf2

set -euo pipefail
cd "$(dirname "$0")"

IMAGE=docker.io/zmkfirmware/zmk-build-arm:4.1-branch
WS="$HOME/.cache/zmk-ws-main"
BOARD="xiao_ble//zmk"
# podman (Arch) or docker (WSL/Ubuntu); override with CONTAINER_ENGINE=...
ENGINE="${CONTAINER_ENGINE:-$(command -v podman || command -v docker || true)}"
[ -n "$ENGINE" ] || { echo "need podman or docker on PATH" >&2; exit 1; }

update=0
targets=()
for arg in "$@"; do
  case "$arg" in
    --update) update=1 ;;
    right|left|dongle|reset|bt60|bt60_reset) targets+=("$arg") ;;
    *) echo "unknown target: $arg (valid: right left dongle reset bt60 bt60_reset, --update)" >&2; exit 1 ;;
  esac
done
[ ${#targets[@]} -gt 0 ] || targets=(right left dongle reset bt60 bt60_reset)

build_cmds=""
for t in "${targets[@]}"; do
  case "$t" in
    right)
      build_cmds+='west build -p -s zmk/app -d build/right -b "'$BOARD'" -- \
        -DZMK_CONFIG=/ws/config -DSHIELD="toucan_right rgbled_adapter" -DZMK_EXTRA_MODULES=/work
' ;;
    left)
      build_cmds+='west build -p -s zmk/app -d build/left -b "'$BOARD'" -- \
        -DZMK_CONFIG=/ws/config -DSHIELD="toucan_left rgbled_adapter toucan_pet" -DZMK_EXTRA_MODULES=/work
' ;;
    dongle)
      # Build one dongle .uf2 per prospector status-screen layout.
      for layout in classic radii field operator; do
        up=$(echo "$layout" | tr '[:lower:]' '[:upper:]')
        build_cmds+='west build -p -s zmk/app -d build/dongle_'$layout' -b "'$BOARD'" -S studio-rpc-usb-uart -- \
        -DZMK_CONFIG=/ws/config -DSHIELD="toucan_dongle rgbled_adapter prospector_adapter" \
        -DZMK_EXTRA_MODULES=/work -DCONFIG_ZMK_STUDIO=y -DCONFIG_PROSPECTOR_STATUS_SCREEN_'$up'=y
'
      done ;;
    reset)
      build_cmds+='west build -p -s zmk/app -d build/reset -b "'$BOARD'" -- \
        -DZMK_CONFIG=/ws/config -DSHIELD="settings_reset" -DZMK_EXTRA_MODULES=/work
' ;;
    bt60)
      build_cmds+='west build -p -s zmk/app -d build/bt60 -b "bt60//zmk" -- \
        -DZMK_CONFIG=/ws/config -DSHIELD="toucan_bt60" -DZMK_EXTRA_MODULES=/work
' ;;
    bt60_reset)
      build_cmds+='west build -p -s zmk/app -d build/bt60_reset -b "bt60//zmk" -- \
        -DZMK_CONFIG=/ws/config -DSHIELD="settings_reset" -DZMK_EXTRA_MODULES=/work
' ;;
  esac
done

update_cmd=""
if [ "$update" = 1 ]; then
  # HTTP/1.1 avoids flaky "HTTP/2 stream not closed cleanly" fetch errors
  update_cmd='git config --global http.version HTTP/1.1
west update --fetch-opt=--filter=tree:0'
fi

mkdir -p "$WS" firmware

"$ENGINE" run --rm -v "$PWD":/work:ro -v "$WS":/ws "$IMAGE" bash -lc '
  set -e
  cd /ws
  rm -rf config && mkdir config && cp -RL /work/config/* config/ 2>/dev/null || true
  [ -d .west ] || west init -l /ws/config
  '"$update_cmd"'
  west zephyr-export
  '"$build_cmds"'
'

declare -A out=([right]=toucan_right [left]=toucan_left [reset]=settings_reset [bt60]=toucan_bt60 [bt60_reset]=settings_reset_bt60)
for t in "${targets[@]}"; do
  if [ "$t" = dongle ]; then
    for layout in classic radii field operator; do
      cp "$WS/build/dongle_$layout/zephyr/zmk.uf2" "firmware/toucan_dongle_$layout.uf2"
      echo "firmware/toucan_dongle_$layout.uf2"
    done
  else
    cp "$WS/build/$t/zephyr/zmk.uf2" "firmware/${out[$t]}.uf2"
    echo "firmware/${out[$t]}.uf2"
  fi
done
