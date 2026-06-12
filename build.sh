#!/usr/bin/env bash
# Build ZMK firmware locally (zmk_4 branch: ZMK main / Zephyr 4.1).
#
# Usage:
#   ./build.sh [targets...] [--update]
#
#   targets   any of: right left dongle reset (default: right left dongle)
#   --update  run `west update` first — needed only after changing config/west.yml
#
# Output: firmware/<target>.uf2

set -euo pipefail
cd "$(dirname "$0")"

IMAGE=docker.io/zmkfirmware/zmk-build-arm:4.1-branch
WS="$HOME/.cache/zmk-ws-main"
BOARD="xiao_ble//zmk"

update=0
targets=()
for arg in "$@"; do
  case "$arg" in
    --update) update=1 ;;
    right|left|dongle|reset) targets+=("$arg") ;;
    *) echo "unknown target: $arg (valid: right left dongle reset, --update)" >&2; exit 1 ;;
  esac
done
[ ${#targets[@]} -gt 0 ] || targets=(right left dongle)

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
      build_cmds+='west build -p -s zmk/app -d build/dongle -b "'$BOARD'" -S studio-rpc-usb-uart -- \
        -DZMK_CONFIG=/ws/config -DSHIELD="toucan_dongle rgbled_adapter prospector_adapter" \
        -DZMK_EXTRA_MODULES=/work -DCONFIG_ZMK_STUDIO=y
' ;;
    reset)
      build_cmds+='west build -p -s zmk/app -d build/reset -b "'$BOARD'" -- \
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

podman run --rm -v "$PWD":/work:ro -v "$WS":/ws "$IMAGE" bash -lc '
  set -e
  cd /ws
  rm -rf config && mkdir config && cp -RL /work/config/* config/ 2>/dev/null || true
  [ -d .west ] || west init -l /ws/config
  '"$update_cmd"'
  west zephyr-export
  '"$build_cmds"'
'

declare -A out=([right]=toucan_right [left]=toucan_left [dongle]=toucan_dongle [reset]=settings_reset)
for t in "${targets[@]}"; do
  cp "$WS/build/$t/zephyr/zmk.uf2" "firmware/${out[$t]}.uf2"
  echo "firmware/${out[$t]}.uf2"
done
