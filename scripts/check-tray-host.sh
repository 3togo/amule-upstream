#!/usr/bin/env bash
# Exercise real X11 embedding rather than injecting GtkStatusIcon size signals.
set -euo pipefail
binary=${1:?usage: check-tray-host.sh /path/to/NativeTrayIconTest}
tray=${STALONETRAY:-stalonetray}
log_dir=$(mktemp -d)
tray_pid=
cleanup() {
    if [[ -n "$tray_pid" ]]; then
        kill "$tray_pid" 2>/dev/null || true
        wait "$tray_pid" 2>/dev/null || true
    fi
    rm -rf "$log_dir"
}
trap cleanup EXIT
for size in 22 32 48 64; do
    "$tray" --config /dev/null --icon-size "$size" --slot-size "$size" \
        --geometry 1x1+0+0 --window-type normal >"$log_dir/tray.log" 2>&1 &
    tray_pid=$!
    # The test waits for the host to own the tray selection and embed the icon.
    if ! AMULE_ICON_TEST_REQUIRE_GUI=1 AMULE_TRAY_TEST_SIZE="$size" "$binary"; then
        cat "$log_dir/tray.log" >&2
        exit 1
    fi
    kill "$tray_pid"
    wait "$tray_pid" || true
    tray_pid=
done
