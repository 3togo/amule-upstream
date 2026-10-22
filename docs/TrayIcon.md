# Tray icon backends

Linux wxGTK3 builds with libayatana-appindicator3 (or Canonical's compatible
appindicator3) install `org.amule.aMule-tray-{high,low,disconnected}` in the
hicolor icon theme. Connection changes select the corresponding coloured
artwork. Visibility remains independent: a disconnected icon stays active when
the tray is enabled, and updates cannot bring back a hidden indicator. SNI has
no speed overlay; rates remain available in the window and tray menu.

Without that dependency, wxGTK builds with their own AppIndicator support use
wx's backend. Other wxGTK builds using GTK 2.18+ or GTK3 use a native
GtkStatusIcon adapter for X11 trays. The adapter renders the SVG and speed bar at the size GTK requests,
including changes while running. The matching GTK2 or GTK3 headers and
libraries are discovered explicitly through pkg-config for both `amule` and `amulegui`; headless builds,
Windows and Cocoa do not acquire a GTK dependency. wx builds using GTK older
than 2.18 retain wx's legacy implementation. AppIndicator3 is selected only
for wxGTK3, avoiding incompatible GTK major versions and GTK calls on wxQt
window handles.

Cocoa uses an NSStatusItem with monochrome template artwork: a check means high
ID, a padlock means low ID, and a cross means disconnected. AppKit supplies the
light/dark/highlight tint. The speed bar uses the same tint. Rendering uses the
menu bar's logical size and the status item's screen scale. Other platforms
retain the coloured artwork. The adapter uses public wx and AppKit APIs and
keeps wx menu command dispatch.

## Regression checks

`IconArtworkTest` verifies SVG and PNG fallbacks, distinct template alpha masks,
monochrome pixels, and speed bar composition at multiple sizes.
`NativeTrayIconTest` exercises the real GTK adapter with tray resize signals,
click/menu dispatch, icon removal/restoration and callback cleanup. It skips
without a display; `AMULE_ICON_TEST_REQUIRE_GUI=1` makes missing GUI startup a
failure. `NativeTrayIconMacTest` checks the actual displayed NSImage template
flag, logical dimensions, tooltip, menu dispatch and removal/restoration on
Cocoa. A macOS build with a different wx toolkit skips that Cocoa-only test.

Run the GUI tests with a working display. Linux CI can use `xvfb-run -a ctest
--test-dir build -R 'IconArtwork|NativeTrayIcon' --output-on-failure`. The
`scripts/check-tray-backend-build.py build --require-remote-gui` check also
compiles the fallback production consumers even when libayatana is installed.
Theme lookup should additionally be checked after installation in a private
staging prefix, including remote-GUI-only installations. Native platform CI
and desktop smoke checks remain necessary for OS-specific behavior.
