#!/usr/bin/env python3
"""Audit the generated corpus; frame readers are deliberately limited to its formats.
This is a measurement tool, not another production media parser.
"""
import argparse
import json
from pathlib import Path
import subprocess


def mp3_frames(data):
    offset = 0
    if data[:3] == b'ID3':
        offset = 10 + sum((data[6 + i] & 127) << (21 - 7 * i) for i in range(4))
    count = samples = audio_bytes = 0
    xing = False
    rates = [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320]
    while offset < len(data):
        h = int.from_bytes(data[offset:offset + 4], 'big')
        if h >> 21 != 0x7ff or (h >> 19) & 3 != 3 or (h >> 17) & 3 != 1:
            raise ValueError('Expected contiguous MPEG-1 Layer III frames')
        rate_index, sample_index = (h >> 12) & 15, (h >> 10) & 3
        if rate_index in (0, 15) or sample_index == 3:
            raise ValueError('Unsupported frame header')
        sample_rate = [44100, 48000, 32000][sample_index]
        size = 144000 * rates[rate_index] // sample_rate + ((h >> 9) & 1)
        if offset + size > len(data):
            raise ValueError('Truncated MP3 frame')
        side = 17 if ((h >> 6) & 3) == 3 else 32
        marker = offset + 4 + (0 if h & (1 << 16) else 2) + side
        is_xing = count == 0 and data[marker:marker + 4] in (b'Xing', b'Info')
        count += 1
        if is_xing:
            xing = True
        else:
            samples += 1152
            audio_bytes += size
        offset += size
    return dict(frames_including_info=count, has_xing_or_info=xing,
                audio_frame_samples=samples, sample_rate=sample_rate,
                encoded_duration_seconds=samples / sample_rate, audio_frame_bytes=audio_bytes)


def adts_frames(data):
    offset = frames = samples = 0
    rates = [96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350]
    while offset < len(data):
        h = data[offset:offset + 7]
        if len(h) < 7 or h[0] != 255 or h[1] & 0xf6 != 0xf0:
            raise ValueError('Expected ADTS header')
        index = (h[2] >> 2) & 15
        if index >= len(rates) or h[2] >> 6 != 1:
            raise ValueError('Only AAC-LC corpus fixtures are supported')
        sample_rate = rates[index]
        size = ((h[3] & 3) << 11) | (h[4] << 3) | (h[5] >> 5)
        if size < 7 or offset + size > len(data):
            raise ValueError('Truncated ADTS frame')
        samples += 1024 * ((h[6] & 3) + 1)
        frames += 1
        offset += size
    return dict(frames=frames, samples=samples, sample_rate=sample_rate,
                duration_seconds=samples / sample_rate)


def flac_comments(data):
    assert data[:4] == b'fLaC'
    offset = 4
    while offset < len(data):
        kind, size = data[offset] & 127, int.from_bytes(data[offset + 1:offset + 4], 'big')
        block = data[offset + 4:offset + 4 + size]
        if kind == 4:
            p = 4 + int.from_bytes(block[:4], 'little')
            count = int.from_bytes(block[p:p + 4], 'little'); p += 4
            comments = []
            for _ in range(count):
                length = int.from_bytes(block[p:p + 4], 'little'); p += 4
                comments.append(block[p:p + length].decode('utf-8')); p += length
            return comments
        offset += 4 + size
    raise ValueError('No Vorbis comments')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('corpus', type=Path)
    p.add_argument('--ffmpeg', default='ffmpeg')
    p.add_argument('--report', type=Path, required=True)
    args = p.parse_args()
    report = {}
    for name in ('audio.mp3', 'audio-no-xing.mp3', 'cover.mp3', 'audio.aac'):
        path = args.corpus / name
        data = path.read_bytes()
        result = adts_frames(data) if name.endswith('.aac') else mp3_frames(data)
        # Decoder applies gapless skip/discard information when available.
        decoded = subprocess.check_output([args.ffmpeg, '-v', 'error', '-nostdin', '-i', str(path),
                                           '-map', '0:a:0', '-ac', '1', '-ar', '48000', '-f', 's16le', '-'], timeout=30)
        seconds = len(decoded) / 96000
        result.update(file_bytes=len(data), decoded_samples_48000=len(decoded) // 2,
                      decoded_duration_seconds=seconds,
                      file_average_bitrate_bps=len(data) * 8 / seconds)
        report[name] = result
    raw = subprocess.check_output([args.ffmpeg, '-v', 'error', '-nostdin', '-i', str(args.corpus / 'video.h264'),
                                   '-map', '0:v:0', '-fps_mode', 'passthrough', '-pix_fmt', 'yuv420p',
                                   '-f', 'rawvideo', '-'], timeout=30)
    count = len(raw) // (64 * 64 * 3 // 2)
    report['video.h264'] = dict(decoded_frames=count, generator_fps=25,
                               intended_duration_seconds=count / 25,
                               caveat='25 fps is generator provenance, not a general inference from raw H.264')
    report['unicode_flac_comments'] = flac_comments((args.corpus / '特殊 name.flac').read_bytes())
    args.report.write_text(json.dumps(report, indent=2, ensure_ascii=True) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2, ensure_ascii=True))


if __name__ == '__main__':
    main()
