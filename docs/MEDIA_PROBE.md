# Bundled media metadata extraction

The core launches `amule-media-probe` beside its own executable. MediaInfoLib is
primary; libavformat/libavcodec/libavutil run in a fresh process if the primary
fails or lacks duration or codec. A complete MediaInfo result wins even when
libav would disagree. Fallback replaces the whole result only if it improves
missing duration/codec without losing either existing field. Tags are not merged.
The old `ffprobe_path` setting is accepted for compatibility but ignored. ffprobe
is needed only to generate comparison evidence, not at runtime.

The helper consumes an already-open, seekable regular file on stdin. The parent
validates a bounded, versioned nine-line response and retains the existing
six public fields: duration (rounded seconds), bitrate (kbps), codec, artist,
album and title. The existing metadata queue and enable/refresh controls remain.
Half the overall deadline is reserved for the primary attempt, leaving the
remaining budget for fallback. Cancellation terminates the child. Bundled library
search paths are preserved. libav secondary resource opens are denied. This is
process isolation and resource limiting, not an operating-system sandbox.

## Build and tests

Install MediaInfoLib plus FFmpeg development libraries (`libmediainfo-dev`,
`libavformat-dev`, `libavcodec-dev`, `libavutil-dev` on Debian/Ubuntu). CMake builds
and installs the helper by default with either core target. Use
`-DENABLE_MEDIA_PROBE=OFF` to omit it; extraction requires the sibling executable.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j2
ctest --test-dir build -R MediaProbe --output-on-failure
```

The native backend tests generate WAV fixtures without ffmpeg. The parent-runner
suite covers primary selection, real WAV extraction, rejection, crash, malformed
output, partial results, deadline fallback and preserving the primary on fallback
failure. It also verifies cancellation, strict protocol fields and that helper
malfunctions never persist as file verdicts. Native tests cover sample-based duration
rounding, subsecond audio, empty input, invalid UTF-8, Unicode and large tags. An
optional ffmpeg/x264 test generates raw AVC streams. The real WAV runner test is
portable; POSIX shell-based failure tests do not certify Windows process behavior.

To reproduce the comparison (requires ffmpeg with x264, LAME, Vorbis and Opus):

```sh
python3 tools/media-probe-compare/make_corpus.py /tmp/media-corpus
python3 tools/media-probe-compare/benchmark_native.py /tmp/media-corpus \
  --helper "$PWD/build/src/amule-media-probe" --report /tmp/native-results.json
```

Use a new corpus directory: generation deliberately refuses to overwrite files.
The WMA fixture exercises real fallback because the MediaInfo codec mapping does
not yet include WMA. The report preserves native results, selected backend,
per-field differences and five-run process timings. Performance measurements are
benchmarks, not pass/fail unit-test thresholds. ffprobe agreement is a compatibility
measure, not proof of correctness.

## Known differences and scope

The native helper does not use the experimental Python MP3/AAC/H.264 scanners.
MediaInfo and libav can disagree about encoder padding, raw-stream duration and
bitrate. The MediaInfo backend deliberately returns unknown duration and bitrate
for raw H.264: fast parsing was observed to report a partial timeline as a complete
duration. Codec identification remains available, and libav is still attempted;
it also reports unknown duration for the current raw AVC fixtures. Reliable timing
for these streams is deferred. For other formats a plausible but incorrect primary
duration does not trigger fallback.
The historical `--policy measured` comparison applies shared corrections and
must not be cited as native-backend agreement or production behavior.

`tools/media-probe-compare/native-results.json` records a local Linux run over 21
synthetic files. See its summary for exact agreement and timings; these small,
warm-cache fixtures are not representative of large files or network shares.
The chain timing is an estimate from backend medians; it excludes the parent's
polling overhead. No package-size advantage is claimed: both libraries are shipped.

## Packaging

AppImage, Windows and macOS recipes include the helper and its dynamic dependencies;
CI installs the corresponding distro/MSYS2/Homebrew packages. Flatpak and the Alpine
static bundle build reduced FFmpeg, MediaInfoLib and ZenLib from checked archives.
`packaging/media/pins.json` pins the static sources; the Flatpak manifests carry the
same URLs and checksums. Update both together. FFmpeg network protocols, encoders,
muxers, external autodetection and unrelated libraries are disabled in these source
builds. The distro builds may contain additional components.

```sh
packaging/media/build-static.sh /tmp/media-prefix /tmp/media-sources
PKG_CONFIG_PATH=/tmp/media-prefix/lib/pkgconfig cmake -S src/mediaprobe \
  -B /tmp/media-helper -DCMAKE_PREFIX_PATH=/tmp/media-prefix -DMEDIA_PROBE_STATIC=ON
cmake --build /tmp/media-helper -j2
ctest --test-dir /tmp/media-helper --output-on-failure
```

The helper installs third-party notices under `share/licenses/amule-media-probe`;
macOS places them under the app's Resources. Upstream source URLs and hashes are
recorded in the pins. Distributors must retain corresponding source/build materials
for their actual bundled versions, including any platform-package configuration
and additional third-party components. Static redistribution also needs the
materials required for relinking under the applicable library licenses; these
notices alone are not a complete release compliance package.

Validated locally: Linux GUI and daemon compilation, native backend and parent
integration tests, pinned static-library dependency build and helper tests.
The native helper tests also pass under AddressSanitizer and UndefinedBehaviorSanitizer.
Windows/macOS packaging, Flatpak runtime and the fully static Alpine image require
platform CI validation; their recipes have been updated but were not executed here.
