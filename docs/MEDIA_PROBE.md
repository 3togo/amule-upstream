# Bundled media metadata extraction

The core launches `amule-media-probe` beside its own executable. The helper uses
MediaInfoLib only. It consumes an already-open, seekable regular file on stdin;
the parent validates a bounded, versioned nine-line response containing duration
(rounded seconds), bitrate (kbps), codec, artist, album and title. The existing
metadata queue and enable/refresh controls remain.

The parent enforces a wall-clock deadline and cancellation, and terminates the
child on timeout. Windows uses a Job object and an explicit inherited handle
list. POSIX builds limit CPU, memory and core dumps. Bundle library search paths
are preserved. This is process isolation and resource limiting, not an operating
system sandbox. Pinned bundle builds disable curl and libmms readers.

`MediaMetadata/FFProbePath` is obsolete: a nonempty old setting is logged once
and the key is removed from `amule.conf`. The EC tag stays reserved and the frozen
`/api/v1` field `files.ffprobe_path` accepts strings as a no-op and reads as empty.
The old path, Browse and Detect controls are removed.

Files with a legacy ffprobe failure marker (`FT_MEDIA_PROBE_FAILED=1`) receive
one retry. Only a MediaInfo file verdict writes marker `2`; the normal scheduling
path skips only `2`. Refresh ignores markers and success clears them. Missing
helpers, crashes, malformed output and cancellation never become file verdicts.

## Build and tests

Install MediaInfoLib (`libmediainfo-dev` on Debian/Ubuntu, `libmediainfo-devel` on
Fedora, `libmediainfo` on Arch or Homebrew, `mingw-w64-<toolchain>-libmediainfo` on MSYS2). CMake builds
and installs the helper by default with either core target. Configure
`-DENABLE_MEDIA_PROBE=OFF` to omit it; extraction needs the sibling executable.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j2
ctest --test-dir build -R MediaProbe --output-on-failure
```

The real-library tests generate WAV fixtures without extra dependencies. They
cover rounding, subsecond audio, empty and malformed files, Unicode, invalid tag
encoding, large tags and protocol framing. Optional ffmpeg-generated fixtures
cover raw AVC, Xvid, ProRes, H.263, WMA and AVI/FLV metadata. ffmpeg is a test
fixture generator only. The runner tests cover cancellation, timeout, crashes,
strict output validation, partial results and file-verdict classification. The
real WAV runner test is portable; shell-based failure tests run on POSIX.

For a short comparison, generate a tagged fixture and run both tools:

```sh
ffmpeg -f lavfi -i sine=duration=3 -metadata artist=Example /tmp/probe.wav
build/src/amule-media-probe --json < /tmp/probe.wav
ffprobe -v quiet -show_format -show_streams -of json /tmp/probe.wav
```

ffprobe agreement measures compatibility, not correctness. MediaInfo can differ
on encoder padding and bitrate accounting. For raw H.264, MediaInfo's duration
can describe only inspected frames even at full parse speed, so the helper
publishes codec but leaves duration and derived bitrate unknown. Reliable raw
H.264 timing remains follow-up work. Files lacking duration get a full scan up
to 512 MiB, bounded by the parent's watchdog.

## Packaging

All bundles build MediaInfoLib and ZenLib from checksum-pinned source archives.
AppImage, macOS, Windows and static Alpine use
`packaging/media/build-static.sh`; Flatpak manifests use the same pins and
explicitly disable curl and libmms. AppImage, macOS and Windows link these two
libraries statically into the helper to avoid platform package dependency
closures. No FFmpeg runtime or development libraries are needed.

```sh
packaging/media/build-static.sh /tmp/media-prefix /tmp/media-sources
cmake -S src/mediaprobe -B /tmp/media-helper \
  -DMEDIAINFO_LIBRARY=/tmp/media-prefix/lib/libmediainfo.a \
  -DMEDIA_ZEN_LIBRARY=/tmp/media-prefix/lib/libzen.a -DMEDIA_PROBE_STATIC=ON
cmake --build /tmp/media-helper -j2
ctest --test-dir /tmp/media-helper --output-on-failure
```

The helper installs MediaInfoLib, ZenLib, aes-gladman and tinyxml2 notices under
`share/licenses/amule-media-probe`; macOS places them in the app's Resources.
See `docs/THIRDPARTY.md`. Update the source pins and both Flatpak manifests
together. Keep the exact sources and build recipes with redistributed bundles.
Windows/macOS packaging, Flatpak runtime and the fully static Alpine image need
validation on their respective platforms.
