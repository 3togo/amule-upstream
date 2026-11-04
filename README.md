# Flat progress-bar default: screenshot evidence

Real aMule wxGTK application captures at source commit `3b5f3397adfe0c6889a267afb12ac20a764ce493`, based on upstream `8e2afa46b`.

- `rounded.png`: saved `eMule/3DDepth=10`, the previous fallback value. Captured with the new binary to demonstrate that existing rounded preferences remain respected.
- `flat.png`: `3DDepth` absent at first launch, selecting the new flat default.
- Both profiles contain the same offline sample download, with no transferred data. These compare styles using the same binary, not two different builds.

Environment: Linux, wxGTK/wxWidgets 3.2.11, default GTK theme, Xvfb 1024×768×24, default display scale, English. No window manager; full root-window screenshots. Native Windows/macOS and dark-theme appearance were not tested.

Build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_MONOLITHIC=ON -DBUILD_REMOTEGUI=ON -DBUILD_ED2K=OFF -DENABLE_UPNP=OFF -DBUILD_TESTING=OFF
cmake --build build --target amule amulegui -j 8
```

To reproduce using the source checkout at the commit above (requires Xvfb, ImageMagick, and its Python integration-test helpers):

```sh
python3 capture.py /absolute/path/to/source /absolute/path/to/output
```

The harness uses isolated offline profiles and checks persisted depth after graceful shutdown. Cases passed: missing value → 0, flat restart → 0, saved 10 → 10 and restart → 10, saved 0/1/5 unchanged. It does not connect to public networks. Display :187 must be free. Temporary profiles are retained for inspection; their paths are not screenshot assets.
