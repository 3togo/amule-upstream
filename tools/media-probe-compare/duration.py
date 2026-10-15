"""Bounded, backend-independent duration evidence for elementary audio/AVC.

Deliberately narrow: MPEG-1 Layer III, ADTS AAC-LC and progressive AVC timing. Unsupported,
truncated or inconsistent streams never yield a measured duration. No decoding,
resynchronization, baseline values or filename-extension assumptions are used.
"""
from decimal import Decimal
import os
import time
from h264_timing import measure as h264, H264Error


class ScanError(ValueError):
    pass


class Reader:
    def __init__(self, stream, max_bytes, timeout):
        self.stream = stream
        self.size = os.fstat(stream.fileno()).st_size
        self.deadline = time.monotonic() + timeout
        if self.size > max_bytes:
            raise ScanError('scan byte limit exceeded')

    def read(self, offset, count):
        if time.monotonic() >= self.deadline:
            raise ScanError('scan deadline exceeded')
        if offset < 0 or offset + count > self.size:
            raise ScanError('truncated input')
        self.stream.seek(offset)
        data = self.stream.read(count)
        if len(data) != count:
            raise ScanError('input changed or read failed')
        return data


def audio_start(r):
    if r.size >= 10 and r.read(0, 3) == b'ID3':
        header = r.read(0, 10)
        if header[3] not in (3, 4) or any(b & 128 for b in header[6:10]):
            raise ScanError('unsupported ID3 header')
        size = sum(b << (21 - 7 * i) for i, b in enumerate(header[6:10]))
        offset = 10 + size + (10 if header[3] == 4 and header[5] & 16 else 0)
        if offset > r.size:
            raise ScanError('truncated ID3 tag')
        return offset
    return 0


def mp3(r, offset, max_frames):
    end = r.size
    if end >= 128 and r.read(end - 128, 3) == b'TAG':
        end -= 128  # ID3v1 has no audio samples.
    frames = audio_frames = 0
    sample_rate = None
    advertised_frames = delay = padding = None
    bitrates = (0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320)
    encoder = None
    while offset < end:
        if frames >= max_frames:
            raise ScanError('scan frame limit exceeded')
        h = int.from_bytes(r.read(offset, 4), 'big')
        if h >> 21 != 0x7ff or (h >> 19) & 3 != 3 or (h >> 17) & 3 != 1:
            raise ScanError('not contiguous MPEG-1 Layer III')
        b, s = (h >> 12) & 15, (h >> 10) & 3
        if b in (0, 15) or s == 3:
            raise ScanError('unsupported MP3 header')
        rate = (44100, 48000, 32000)[s]
        if sample_rate is not None and rate != sample_rate:
            raise ScanError('sample rate changed')
        sample_rate = rate
        size = 144000 * bitrates[b] // rate + ((h >> 9) & 1)
        if offset + size > end:
            raise ScanError('truncated MP3 frame')
        frame = r.read(offset, size)
        marker = 4 + (17 if (h >> 6) & 3 == 3 else 32)
        if not h & (1 << 16):
            # CRC-protected Xing layouts vary; do not guess the metadata position.
            raise ScanError('CRC-protected MP3 not supported by this prototype')
        is_info = frame[marker:marker + 4] in (b'Xing', b'Info')
        if is_info:
            if frames:
                raise ScanError('concatenated MP3 Info/Xing stream')
            flags = int.from_bytes(frame[marker + 4:marker + 8], 'big')
            if flags & ~15 or marker + 8 > size:
                raise ScanError('invalid Xing flags')
            pos = marker + 8
            for flag, length in ((1, 4), (2, 4), (4, 100), (8, 4)):
                if flags & flag:
                    if pos + length > size:
                        raise ScanError('truncated Xing metadata')
                    if flag == 1:
                        advertised_frames = int.from_bytes(frame[pos:pos + 4], 'big')
                    pos += length
            if frame[pos:pos + 4] in (b'LAME', b'Lavc', b'Lavf'):
                if pos + 24 > size:
                    raise ScanError('truncated encoder padding')
                encoder = frame[pos:pos + 9].rstrip(b'\0').decode('ascii', 'replace')
                packed = int.from_bytes(frame[pos + 21:pos + 24], 'big')
                delay, padding = packed >> 12, packed & 4095
        else:
            audio_frames += 1
        frames += 1
        offset += size
    if not audio_frames:
        raise ScanError('no audio frames')
    if advertised_frames is not None and advertised_frames != audio_frames:
        raise ScanError('Xing frame count does not match complete scan')
    samples = audio_frames * 1152
    if delay is not None:
        if advertised_frames is None or delay + padding >= samples:
            raise ScanError('unverifiable encoder padding')
        samples -= delay + padding
    return {'status': 'measured', 'format': 'mp3', 'method': 'gapless_frame_count' if delay is not None else 'frame_count',
            'audio_frames': audio_frames, 'sample_rate': sample_rate, 'samples': samples,
            'encoder': encoder, 'encoder_delay': delay, 'encoder_padding': padding,
            'duration_seconds': str(Decimal(samples) / sample_rate)}


def adts(r, offset, max_frames):
    frames = samples = 0
    config = None
    rates = (96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350)
    while offset < r.size:
        if frames >= max_frames:
            raise ScanError('scan frame limit exceeded')
        h = r.read(offset, 7)
        if h[0] != 255 or h[1] & 0xf6 != 0xf0:
            raise ScanError('not contiguous ADTS')
        index = (h[2] >> 2) & 15
        channels = ((h[2] & 1) << 2) | (h[3] >> 6)
        current = (index, channels, h[2] >> 6, (h[1] >> 3) & 1)
        if index >= len(rates) or current[2] != 1 or channels == 0:
            raise ScanError('only AAC-LC with explicit channel configuration supported')
        if config is not None and current != config:
            raise ScanError('ADTS configuration changed')
        config = current
        size = ((h[3] & 3) << 11) | (h[4] << 3) | (h[5] >> 5)
        header_size = 7 if h[1] & 1 else 9
        if size <= header_size or offset + size > r.size:
            raise ScanError('truncated/empty ADTS frame')
        # Multiple raw blocks and implicit SBR/PS require more parsing than this adapter.
        if h[6] & 3:
            raise ScanError('multiple ADTS raw blocks unsupported')
        samples += 1024
        frames += 1
        offset += size
    if not frames:
        raise ScanError('no ADTS frames')
    return {'status': 'measured', 'format': 'aac', 'method': 'frame_count',
            'audio_frames': frames, 'sample_rate': rates[config[0]], 'samples': samples,
            'duration_seconds': str(Decimal(samples) / rates[config[0]])}


def scan(path, max_bytes=512 * 1024 * 1024, max_frames=2_000_000, timeout=10):
    try:
        with open(path, 'rb') as stream:
            r = Reader(stream, max_bytes, timeout)
            offset = audio_start(r)
            if r.size - offset < 4:
                return {'status': 'unsupported', 'reason': 'no elementary audio header'}
            h = r.read(offset, 4)
            if h[0] == 255 and h[1] & 0xfe == 0xfa:
                result = mp3(r, offset, max_frames)
            elif h[0] == 255 and h[1] & 0xf6 == 0xf0:
                result = adts(r, offset, max_frames)
            elif offset == 0 and (h.startswith(b'\0\0\1') or h == b'\0\0\0\1'):
                result = h264(r, max_frames)
            else:
                return {'status': 'unsupported', 'reason': 'not supported elementary audio or Annex-B AVC'}
            if os.fstat(stream.fileno()).st_size != r.size:
                raise ScanError('input size changed')
            return dict(result, file_bytes=r.size)
    except (OSError, ScanError, H264Error) as exc:
        return {'status': 'unavailable', 'reason': str(exc)}
