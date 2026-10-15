#!/usr/bin/env python3
"""Generate a small deterministic synthetic corpus with a local FFmpeg executable."""
import argparse
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('directory', type=Path)
p.add_argument('--ffmpeg', default='ffmpeg')
a = p.parse_args()
a.directory.mkdir(parents=True, exist_ok=True)
cases = [
    ('audio.wav', ['-c:a', 'pcm_s16le']),
    ('audio.flac', ['-c:a', 'flac']),
    ('audio.mp3', ['-c:a', 'libmp3lame']),
    ('audio-no-xing.mp3', ['-c:a', 'libmp3lame', '-q:a', '3', '-write_xing', '0']),
    ('audio.ogg', ['-c:a', 'libvorbis']),
    ('audio.opus', ['-c:a', 'libopus']),
    ('audio.m4a', ['-c:a', 'aac']),
    ('audio.aac', ['-c:a', 'aac']),
    ('audio.wma', ['-c:a', 'wmav2']),
    ('audio.aiff', ['-c:a', 'pcm_s16be']),
    ('audio.mka', ['-c:a', 'flac']),
]
for name, options in cases:
    subprocess.run([a.ffmpeg, '-v', 'error', '-nostdin', '-n', '-f', 'lavfi', '-i',
                    'sine=frequency=440:sample_rate=48000:duration=2.6',
                    '-metadata', 'artist=Test Artist', '-metadata', 'album=Test Album',
                    '-metadata', 'title=Test Title', *options, str(a.directory / name)], check=True)
for extension in ('mp4', 'mkv', 'h264'):
    subprocess.run([a.ffmpeg, '-v', 'error', '-nostdin', '-n', '-f', 'lavfi', '-i',
                    'testsrc2=size=64x64:rate=25:duration=2.6', '-c:v', 'libx264',
                    '-threads', '1', '-metadata', 'title=Video Title',
                    str(a.directory / ('video.' + extension))], check=True)
# Stream-label and multi-track cases exercise the restricted tag fallback.
for name, codec, count in [('label.mka', 'aac', 1), ('multi.mka', 'flac', 2)]:
    maps = ['-map', '0:a'] * count
    subprocess.run([a.ffmpeg, '-v', 'error', '-nostdin', '-n', '-f', 'lavfi', '-i',
                    'sine=duration=2.6', *maps, '-c:a', codec,
                    '-metadata:s:a:0', 'title=Deutsch', str(a.directory / name)], check=True)
# Real video plus audio and container tags.
subprocess.run([a.ffmpeg, '-v', 'error', '-nostdin', '-n', '-i', str(a.directory / 'video.mp4'),
                '-i', str(a.directory / 'audio.flac'), '-map', '0:v', '-map', '1:a',
                '-c', 'copy', '-metadata', 'artist=Video Artist',
                str(a.directory / 'mixed.mkv')], check=True)
# Artwork must not replace the audio codec.
# Build the still separately: -frames:v 1 on the combined mux can stop audio early.
import tempfile
with tempfile.TemporaryDirectory() as temporary:
    cover = Path(temporary) / 'cover.jpg'
    subprocess.run([a.ffmpeg, '-v', 'error', '-nostdin', '-n', '-f', 'lavfi', '-i',
                    'color=red:size=32x32', '-frames:v', '1', str(cover)], check=True)
    subprocess.run([a.ffmpeg, '-v', 'error', '-nostdin', '-n', '-i', str(a.directory / 'audio.mp3'),
                    '-i', str(cover), '-map', '0:a', '-map', '1:v', '-c', 'copy',
                    '-disposition:v', 'attached_pic', str(a.directory / 'cover.mp3')], check=True)
# Quoting, Unicode, control characters and length normalization.
subprocess.run([a.ffmpeg, '-v', 'error', '-nostdin', '-n', '-i', str(a.directory / 'audio.flac'),
                '-c', 'copy', '-metadata', 'artist=艺术家', '-metadata', 'album=' + 'A' * 300,
                '-metadata', 'title=Line one\nLine two "quoted" \\ =',
                str(a.directory / '特殊 name.flac')], check=True)
# Explicit in-stream CFR timing, including a fractional rate and multiple slices.
# Keep video.h264 above as the non-fixed-clock/unknown-duration regression case.
for name, rate, frames, size, parameters in [
    ('video-cfr.h264', '25', 65, '64x64', 'force-cfr=1'),
    ('video-cfr-multislice.h264', '30000/1001', 90, '128x96', 'force-cfr=1:slices=4:pic-struct=1'),
]:
    subprocess.run([a.ffmpeg, '-v', 'error', '-nostdin', '-n', '-f', 'lavfi', '-i',
                    'testsrc2=size=' + size + ':rate=' + rate, '-frames:v', str(frames),
                    '-c:v', 'libx264', '-threads', '1', '-x264-params', parameters,
                    str(a.directory / name)], check=True)
