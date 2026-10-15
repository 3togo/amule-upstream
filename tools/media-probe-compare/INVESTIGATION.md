# Investigation of the six initial differences

Measurements on the development Linux host, 2026-10-15. The corrected corpus
comes from `make_corpus.py`; audit it with:

```sh
python3 tools/media-probe-compare/investigate.py /tmp/media-probe-corpus \
  --report /tmp/media-probe-audit.json
python3 tools/media-probe-compare/compare.py /tmp/media-probe-corpus \
  --mi-parse-speed 1 --policy file-average --report /tmp/media-probe-normalized.json
```

The audit counts MPEG-1 Layer III frames and AAC-LC ADTS frames directly from the
bytes, cross-checks decoded sample counts with FFmpeg, reads FLAC Vorbis comments
directly, and counts decoded H.264 pictures. The tiny frame readers only support
the generated fixtures. They are not proposed as production parsers.

## What “correct bitrate” means

The current application requests overall/container bitrate, not audio elementary
stream bitrate. For a consistent explicit definition, use:

    floor(file_size_bytes * 8 / unrounded_playback_duration_seconds / 1000)

This includes metadata, artwork and container overhead. A 64 kbps audio stream
can therefore have a higher file-average bitrate. Never divide by the already
rounded integer duration published in the result. If duration is unavailable,
file-average bitrate is also unavailable. Other definitions are reasonable, but
switching to audio bitrate changes the application contract and needs a rule for
video/multiple streams.

## Findings

| Fixture | Evidence | Correct interpretation |
| --- | --- | --- |
| Ordinary MP3 | 21,424 bytes; 110 audio frames plus one Info frame; 126,720 encoded audio samples; gapless decoding yields 124,800 samples at 48 kHz = 2.600 s | File-average bitrate = 65,920 bps, published as 65 kbps. FFmpeg is correct for this definition. MediaInfo's reported 64,000 is the audio bitrate. MediaInfo's 2.640 s general duration includes padding; recalculating with it still produces only 64 kbps. |
| MP3 without Xing | 11,344 bytes; 110 audio frames; 126,720 samples at 48 kHz = 2.640 s, corroborated by decoding | Without gapless information the encoded playback is 2.640 s, even though the generator started with 2.600 s of source. File-average bitrate is 34,375.76 bps, published as 34 kbps. MediaInfo is correct. FFmpeg estimates 2.543694 s and consequently 35,677 bps. |
| MP3 with artwork | Original fixture was inadvertently truncated by `-frames:v 1` on the combined mux. Corrected fixture: 21,678 bytes, same 124,800 decoded samples as the original full MP3 | Corrected file-average bitrate = 66,701.54 bps, published as 66 kbps. FFmpeg is correct. MediaInfo reports audio bitrate 64 kbps; recalculating from its padded 2.640 s produces 65 kbps. The old 209 kbps observation concerned the broken short fixture and must not be used to characterize normal cover-art overhead. |
| Raw AAC | 123 ADTS AAC-LC frames, 1,024 samples each, 48 kHz = 2.624 s; decoding agrees; 23,703 bytes | Correct file-average bitrate = 72,265.24 bps, published as 72 kbps. MediaInfo ParseSpeed=1 recovers these values. At 0.5 it omits them. FFmpeg estimates 2.594710 s, producing 73,080 bps. Both measured and estimated durations round to 3 s, hiding the duration discrepancy in the public field. |
| Raw H.264 | 65 decoded pictures; source generated at 25 fps for 2.600 s; MediaInfo reports 2.360 s / 59,963 bps at both parse speeds | For this known fixture, intended duration is 2.600 s (3 rounded), and intended file-average bitrate is 54,427.69 bps (54 kbps). MediaInfo's estimate is wrong. FFmpeg returns unknown, which is safer. The SPS has time_scale=50, num_units_in_tick=1, fixed_frame_rate_flag=0; neither dividing frame count by ffprobe's r_frame_rate=50 nor blindly assuming a constant rate is a general solution. Source provenance establishes this fixture's intended timing, not arbitrary raw H.264 timing. |
| Newline-containing FLAC title | Direct Vorbis-comment parsing verifies a real newline between the two phrases, plus a literal backslash | FFmpeg preserves the stored value; aMule removes the newline during sanitization. MediaInfo replaces it with ` / ` by default. Disable CarriageReturnReplace and retrieve tags through MediaInfo_Get; all backends then produce identical sanitized titles without damaging genuine slashes/backslashes. |

The MediaInfo default and replacement code were checked in upstream
[MediaInfo_Config.cpp](https://github.com/MediaArea/MediaInfoLib/blob/master/Source/MediaInfo/MediaInfo_Config.cpp)
and [File__Analyze_Streams.cpp](https://github.com/MediaArea/MediaInfoLib/blob/master/Source/MediaInfo/File__Analyze_Streams.cpp).
With replacement disabled, Inform(JSON) uses base64 for control-containing strings
and can expose internal escape placeholders. Direct Get avoids relying on that
presentation encoding. The harness retains the JSON and adds `_native_tags` so
both representations remain available for diagnosis.

## Implemented normalization and remaining limits

1. Corrected cover-art generation: create the JPEG separately, then copy both
   streams into the MP3. Verify audio decoded sample count equals the original.
2. Preserve MediaInfo newlines and retrieve native tags before the common sanitizer.
   Literal ` / ` and backslash sequences are preserved.
3. Add `--mi-parse-speed 1` to evaluate full scans under the existing timeout. This
   is explicit; default 0.5 remains available for comparison. Full parsing recovers
   AAC here but does not fix H.264 or MP3 gapless semantics.
4. Add opt-in `--policy file-average`, deriving bitrate from each backend's own
   unrounded duration and the same file size. Default `legacy` retains reported
   bitrate. Reports retain `legacy_result`, raw data and policy settings.

On the corrected corpus, 13/18 match across all three. FFmpeg executable and
library match on all 18. With full parsing and file-average policy the remaining
five are the three MP3s, AAC and H.264. AAC's published duration now matches;
its bitrate still differs. Matching rounded durations is not sufficient evidence
that precise durations agree.

Do not force equality by borrowing the baseline's duration or overwriting an
unknown value with an estimate from another backend. That would stop testing
independent implementations. The unresolved work is duration semantics:

* distinguish measured frame/sample duration, gapless playback duration and estimates;
* support trustworthy encoder-delay/padding interpretation for MP3;
* provide bounded full-scan/sample-count paths for MP3 without Xing and raw AAC;
* leave raw H.264 duration unknown unless timing evidence is sufficient.

These require additional extraction logic and a provenance/confidence contract,
not another conversion between units. Full decoding is useful as a test oracle
here; it is not enabled in the production-style comparison path. Tests and this
small synthetic corpus do not establish accuracy on arbitrary real files.

## Frame-counting follow-up

The opt-in `--policy measured` now adds a shared, bounded elementary-audio scanner
(`duration.py`) before result normalization. It reads the actual file, never the
ffprobe baseline's duration. The native results remain in `backend_result` and
`backend_differences`, so agreement introduced by common code is explicit.

For supported MPEG-1 Layer III, it counts complete audio frames (1,152 samples
per frame), excludes the initial Xing/Info frame, checks the advertised frame
count against the scan, and subtracts encoder delay plus end padding for recognized
LAME/Lavc/Lavf tags. Both generated tagged MP3s carry delay=576 and padding=1344;
126,720 encoded samples minus 1,920 gives 124,800 samples = 2.600 seconds. The
MP3 without Xing has no trustworthy gapless trim and retains 126,720 samples =
2.640 seconds. The tag interpretation was cross-checked against
[FFmpeg's MP3 demuxer](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/mp3dec.c).

For supported ADTS AAC-LC, it counts complete single-block frames at 1,024 samples
per frame and validates a constant sample-rate/channel configuration. The fixture
has 123 frames = 125,952 samples = 2.624 seconds. There is no invented AAC padding
trim: this raw stream does not provide the original source's gapless edit metadata.

All four measured sample counts agree with the earlier decode audit. The selected
file-average policy then yields:

| File | Measured duration | Published seconds | Published bitrate |
| --- | --- | --- | --- |
| audio.mp3 | 2.600 s | 3 | 65 kbps |
| audio-no-xing.mp3 | 2.640 s | 3 | 34 kbps |
| cover.mp3 | 2.600 s | 3 | 66 kbps |
| audio.aac | 2.624 s | 3 | 72 kbps |

Raw H.264 is deliberately **unknown** under this policy, with both duration and
derived bitrate absent (0 in the existing result structure). This prototype does
not yet implement a trustworthy picture-timing analysis for raw H.264, even for
streams that might provide sufficient timing. It does not use the generator's
25 fps or manufacture a duration from frame count alone. Codec metadata remains.
This is a conservative policy, not recovery of the fixture's known 2.600 seconds.

Rerun: **18/18 normalized results match**, comprising 13 independent backend-only
matches, four shared audio corrections and one shared unknown-duration policy.
There are still only **13/18 backend-only matches**. This establishes agreement
for this shared normalization layer on the corpus; it does not establish that the
three native parsers independently determine the same duration.

Seventeen unit tests pass, including frame-count/padding validation, rate changes,
truncation, ID3 exclusion, scan limits, raw-video unknowns and unchanged container
results. An end-to-end one-frame scan limit correctly reports a failed comparison
instead of counting matching unknown values as success.

The scanner runs in a separate process with the harness timeout (30 seconds by
default), plus cooperative read checks, a 512 MiB file-size limit and a two-million
frame limit. It rejects unsupported/truncated/inconsistent input rather than
resynchronizing or extrapolating. Current exclusions include MPEG-2/2.5 Layer III,
free-format MP3, CRC-protected MP3, unrecognized MP3 trailing tags, AAC profiles or
multi-block configurations outside the supported subset. Frame headers do not
prove that compressed payload is decodable; production use still needs broader
validation. This remains standalone prototype code, not a change to aMule.
