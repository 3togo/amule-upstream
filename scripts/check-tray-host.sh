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
    # Allocate the full slot even when GTK2 publishes a smaller natural-size hint.
    # The test checks pixbuf pixels too, so host-side stretching cannot pass it.
    "$tray" --config /dev/null --icon-size "$size" --slot-size "$size" \
        --kludges force_icons_size \
        --geometry 1x1+0+0 --window-type normal >"$log_dir/tray.log" 2>&1 &
    tray_pid=$!
    # Wait for the actual selection owner, rather than racing GTK2's initial
    # manager discovery against stalonetray startup. No fixed startup sleep.
    python3 - <<'READY'
import ctypes
import time

x11 = ctypes.CDLL("libX11.so.6")
x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
x11.XOpenDisplay.restype = ctypes.c_void_p
x11.XDefaultScreen.argtypes = [ctypes.c_void_p]
x11.XDefaultScreen.restype = ctypes.c_int
x11.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
x11.XInternAtom.restype = ctypes.c_ulong
x11.XGetSelectionOwner.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
x11.XGetSelectionOwner.restype = ctypes.c_ulong
x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
display = x11.XOpenDisplay(None)
if not display:
    raise SystemExit("Cannot open X11 display")
try:
    screen = x11.XDefaultScreen(display)
    selection = x11.XInternAtom(display, f"_NET_SYSTEM_TRAY_S{screen}".encode(), False)
    for attempt in range(250):
        if x11.XGetSelectionOwner(display, selection):
            break
        time.sleep(0.02)
    else:
        raise SystemExit("stalonetray did not acquire the X11 tray selection")
finally:
    x11.XCloseDisplay(display)
READY
    if ! AMULE_ICON_TEST_REQUIRE_GUI=1 AMULE_TRAY_TEST_SIZE="$size" "$binary"; then
        cat "$log_dir/tray.log" >&2
        exit 1
    fi
    kill "$tray_pid"
    wait "$tray_pid" || true
    tray_pid=
done
