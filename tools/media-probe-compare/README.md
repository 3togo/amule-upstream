For the integrated MediaInfo-first helper, reproducible native benchmarks and packaging,
see [the production integration notes](../../docs/MEDIA_PROBE.md). The measured-policy
experiments below are separate from the shipped helper.

# Standalone media probe comparison

This experiment compares the six fields aMule publishes using three independent
extraction paths: the `ffprobe` executable, a small C++ program linked to
libavformat/libavcodec/libavutil, and MediaInfoLib loaded through its C API.
It does not change aMule or participate in the main build.

## Build and run

Requirements: Python 3.9+, CMake, a C++17 compiler, pkg-config, FFmpeg development
libraries, an ffprobe executable, and a MediaInfoLib shared library. No Python
packages or MediaInfo development headers are required. Fixture generation also
requires ffmpeg with libx264, libmp3lame, libvorbis and libopus encoders.

```sh
cmake -S tools/media-probe-compare -B tools/media-probe-compare/build
cmake --build tools/media-probe-compare/build
python3 tools/media-probe-compare/make_corpus.py /tmp/media-probe-corpus
python3 tools/media-probe-compare/compare.py /tmp/media-probe-corpus \
  --report /tmp/media-probe-report.json
python3 -m unittest discover -s tools/media-probe-compare -v
```

Run from the repository root, or use absolute script/source paths. The corpus
output must be a fresh directory; the generator refuses to overwrite files.
Input directories are traversed recursively and every regular file is compared.
Keep reports and build outputs outside the corpus directory.

For your own library:

```sh
python3 tools/media-probe-compare/compare.py /path/to/media \
  --libav-probe /path/to/libav-probe \
  --ffprobe /path/to/ffprobe \
  --mediainfo-library /path/to/libmediainfo.so.0 \
  --report /tmp/my-library-report.json
```

`--mediainfo-library` can name MediaInfo.dll on Windows or a dylib on macOS.
On Windows, MSYS2 provides a suitable CMake/pkg-config/FFmpeg toolchain; use
`python` if that is your Python executable. Multi-configuration CMake generators
may require `--libav-probe` pointing into the Release directory. Only Linux has
been exercised so far; Windows/macOS builds and Unicode path behavior still need
validation. This first C++ adapter uses ordinary argv and FFmpeg path opening;
it does not yet prove inherited-handle portability.

Exit status is 0 only when all files compare exactly across all three backends;
1 means a mismatch, unmapped codec or execution failure; 2 means bad CLI usage.
No numerical tolerance is applied. The report includes raw extraction data,
MediaInfo's projected fields, normalized results, exact differing values, elapsed
time, process exit status, stderr, FFmpeg versions and MediaInfo's library version.
Timing includes process startup (including Python startup for MediaInfo), so it
is not a library-performance benchmark.

## Contract

The ffprobe query uses exactly the fields from `MediaProbe::ProbeEntries()` but
requests JSON rather than flat output. Its normalization is a Python port of the
valid-input rules in `src/MediaProbe.cpp`, not a call to aMule's existing parser.
The C++ adapter emits corresponding raw fields; both feed the same normalizer.
MediaInfoLib uses its JSON output with ParseSpeed=0.5 and a separate projection.
Use `--mi-parse-speed 1` for a full-parse experiment; there is no automatic retry.
Text tags are also fetched via MediaInfo_Get with newline replacement disabled.

* Duration: container seconds, nearest integer with positive half values rounded
  up, saturated to uint32. There is no stream-duration fallback.
* Bitrate: overall bits/second divided by 1000, truncated and saturated to uint32.
* Codec: first video stream with a codec, otherwise first audio stream with a
  codec; attached pictures are excluded in the FFmpeg paths.
* Tags: case-insensitive container artist/album/title; stream fallback only for
  a single eligible audio stream, no eligible video and vorbis/opus/flac/speex.
* Text: 256-character cap, control filtering and whitespace trimming.
* Success: positive rounded duration or a selected codec. Tag-only results fail.

The normalizer deliberately rejects non-finite/invalid numbers instead of copying
potentially undefined behavior in the old string parser. Python Unicode counting
and whitespace trimming are not guaranteed to reproduce every wxString edge case.

MediaInfo mapping is intentionally incomplete. Unknown codecs produce an explicit
warning and a failed comparison, never a guessed match. Its Image tracks are not
video candidates; cover art reported differently by particular containers needs
further fixtures. Stream order and container-specific tag promotion also require
real-world validation. Preserve raw output when extending mappings.

## Initial validation (historical, before investigation)

On the development Linux host, FFmpeg 8-era libraries (libavformat 62.12.102,
libavcodec 62.28.102, libavutil 60.26.102) and MediaInfoLib 26.05:

* ffprobe versus libav: all six fields and normalized outcome match on 18 files.
* All three backends: 12 of 18 files match exactly.
* Differences remain for MP3 overall bitrate, raw AAC/H.264 duration and bitrate,
  and newline-containing title handling. These are reported, not rounded away.
* Six normalization/mapping tests pass.

Fixtures include WAV, AIFF, FLAC, MP3 with/without Xing and with cover art,
Vorbis, Opus, M4A, raw AAC, Matroska audio/video, MP4, raw H.264, multiple audio
tracks, track labels, Unicode paths/tags, long tags and control characters.
This synthetic corpus is evidence for a prototype, not proof of general parity.

## Investigation update

The original cover-art fixture accidentally truncated audio; its generator has
been fixed. The six initial differences have now been audited against frame/sample
counts and stored tags. See [INVESTIGATION.md](INVESTIGATION.md) for measured values,
which backend is correct for each case, and remaining duration uncertainties.

The corrected corpus matches across all three backends on **13/18** files;
ffprobe and libav still match on **18/18**. Eight contract tests pass. The title
mismatch is fixed. Full parsing recovers AAC duration, but numerical estimates and
MP3 gapless semantics still cause five file-level differences.

Use `--policy file-average --mi-parse-speed 1` to compare an explicit bitrate
policy: file bytes times eight divided by each backend's unrounded duration.
This is opt-in because it changes the meaning of reported bitrate for some files.
The report keeps `legacy_result` alongside the selected result. There is no forced
parity or numerical tolerance, and missing duration gives missing derived bitrate.

## Scope before production integration

Each parser runs in its own child process with a wall-clock timeout. This is a
local diagnostic tool, not the hardened production helper: output capture is not
size-bounded, there is no sandbox, custom seekable I/O or secondary-open policy,
and native parser failure is recorded as an execution error rather than mapped
to aMule's persistent retry outcomes. Even three failing parsers do not count as
a successful comparison. Do not use this harness to mark files unprobeable.

Next steps are real-library comparisons, expanding verified mappings, testing
supported operating systems, and then extracting the proven contract into the
production helper. Some parser estimates will legitimately remain different;
backend interchangeability does not require concealing those differences.

## Measured-duration policy

```sh
python3 tools/media-probe-compare/compare.py /tmp/media-probe-corpus \
  --policy measured --mi-parse-speed 1 --report /tmp/media-probe-measured.json
```

This adds a **shared** MP3/AAC frame scanner and recognized MP3 encoder-padding
handling, then computes file-average bitrate. It does not borrow values from
ffprobe. It also makes raw H.264 duration/bitrate explicitly unknown, since this
prototype does not establish reliable raw-picture timing. Other containers retain
their backend durations.

Corrected corpus result: **18/18 normalized matches**, but **13/18 backend-only
matches**. Four audio files now have measured duration corrections; one raw video
has unknown duration by policy. The report explicitly distinguishes these counts,
retains each backend's uncorrected result and records duration evidence. See the
[follow-up investigation](INVESTIGATION.md#frame-counting-follow-up) for values and
supported formats. Seventeen unit tests pass.

Scan limits: `--duration-max-bytes` (default 536870912),
`--duration-max-frames` (default 2000000), and `--timeout` (seconds, default 30).
A rejected or timed-out scan for an applicable audio format makes the comparison
fail; matching missing values are not counted as successful measurement.

## H.264 timing experiment (not a production parser)

The measured policy now has an experimental Annex-B scanner in `h264_timing.py`.
It counts progressive pictures rather than slices, reads active SPS/PPS VUI timing,
and supports progressive picture-timing SEI frame doubling/tripling. Constant-rate
streams with reliable timing produce a duration. Missing/non-fixed timing remains
unknown; unsupported field/MBAFF, partitioned/extended AVC and explicit SEI clock
timestamp cases fail with a reason. This is not a complete H.264 implementation.

The corpus now has 20 files: the original 18 plus explicit 25 fps CFR and
30000/1001 fps multi-slice CFR samples. Measured durations are respectively 2.600 s
(65 pictures) and 3.003 s (90 pictures, 360 slices). Picture counts agree with an
independent FFmpeg decode count. All 20 normalized results match, with 13 native
matches, six shared measurements and one unknown-timing file. All 28 unit tests
pass. The original raw H.264 remains unknown because its fixed-rate flag is false.

This hand-written parser is an experiment to test the timing contract, not the
recommended production dependency. Prefer mature parsing implementations:
[FFmpeg's parser API](https://ffmpeg.org/doxygen/trunk/structAVCodecParserContext.html)
or [GStreamer's H.264 parser](https://gstreamer.freedesktop.org/documentation/codecparsers/gsth264parser.html),
which publicly exposes SPS/VUI, slice headers and SEI structures. A separate
application policy must still decide when timing is sufficiently trustworthy.
