"""Conservative Annex-B H.264 timing from progressive picture headers and VUI.

No filename, guessed framerate, generator settings, or other backend is used.
Supports ordinary non-partitioned AVC slices; not a compressed-payload validator.
"""
from decimal import Decimal
from fractions import Fraction
import re
import time


class H264Error(ValueError):
    pass


class Bits:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def get(self, count=1):
        if count < 0 or self.pos + count > len(self.data) * 8:
            raise H264Error('truncated H.264 syntax')
        value = 0
        for _ in range(count):
            value = (value << 1) | ((self.data[self.pos // 8] >> (7 - self.pos % 8)) & 1)
            self.pos += 1
        return value

    def ue(self):
        zeros = 0
        while not self.get():
            zeros += 1
            if zeros > 31:
                raise H264Error('oversized Exp-Golomb value')
        return (1 << zeros) - 1 + self.get(zeros)

    def se(self):
        value = self.ue()
        return (value + 1) // 2 if value & 1 else -(value // 2)


def rbsp(data):
    return re.sub(b'\x00\x00\x03(?=[\x00-\x03])', b'\x00\x00', data)


def hrd(b):
    count = b.ue() + 1
    if count > 32:
        raise H264Error('invalid HRD CPB count')
    b.get(8)
    for _ in range(count):
        b.ue(); b.ue(); b.get()
    b.get(5)
    cpb, dpb = b.get(5) + 1, b.get(5) + 1
    b.get(5)
    return cpb + dpb


def sps(data):
    b = Bits(data)
    profile = b.get(8); b.get(8); b.get(8)
    result = {'id': b.ue(), 'timing': None, 'fixed': False, 'pic_struct': False, 'hrd_bits': 0}
    if result['id'] > 31:
        raise H264Error('invalid SPS id')
    if profile in (100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134, 135):
        chroma = b.ue()
        if chroma > 3 or (chroma == 3 and b.get()):
            raise H264Error('unsupported separate colour planes')
        b.ue(); b.ue(); b.get()
        if b.get():
            for i in range(12 if chroma == 3 else 8):
                if b.get():
                    last = next_value = 8
                    for _ in range(16 if i < 6 else 64):
                        if next_value:
                            next_value = (last + b.se()) % 256
                        last = next_value or last
    elif profile not in (66, 77, 88):
        raise H264Error('unsupported AVC profile')
    result['frame_bits'] = b.ue() + 4
    if result['frame_bits'] > 16:
        raise H264Error('invalid frame_num width')
    result['poc_type'] = b.ue()
    if result['poc_type'] == 0:
        result['poc_bits'] = b.ue() + 4
        if result['poc_bits'] > 16:
            raise H264Error('invalid picture-order width')
    elif result['poc_type'] == 1:
        result['delta_zero'] = b.get()
        b.se(); b.se()
        count = b.ue()
        if count > 255:
            raise H264Error('oversized POC cycle')
        for _ in range(count): b.se()
    elif result['poc_type'] != 2:
        raise H264Error('invalid picture-order type')
    b.ue(); result['gaps_allowed'] = b.get()
    width, height = b.ue() + 1, b.ue() + 1
    result['mb_count'] = width * height
    if not b.get():
        raise H264Error('field/MBAFF timing is not implemented')
    b.get()
    if b.get():
        for _ in range(4): b.ue()
    if not b.get(): return result
    if b.get():
        if b.get(8) == 255: b.get(32)
    if b.get(): b.get()
    if b.get():
        b.get(4)
        if b.get(): b.get(24)
    if b.get(): b.ue(); b.ue()
    if b.get():
        tick, scale = b.get(32), b.get(32)
        result['fixed'] = bool(b.get())
        if not tick or not scale:
            raise H264Error('zero VUI timing value')
        result['timing'] = (tick, scale)
    nal_hrd = bool(b.get())
    if nal_hrd: result['hrd_bits'] = hrd(b)
    vcl_hrd = bool(b.get())
    if vcl_hrd:
        bits = hrd(b)
        if nal_hrd and bits != result['hrd_bits']:
            raise H264Error('conflicting HRD delay widths')
        result['hrd_bits'] = bits
    if nal_hrd or vcl_hrd: b.get()
    result['pic_struct'] = bool(b.get())
    return result


def pps(data):
    b = Bits(data)
    result = {'id': b.ue(), 'sps': b.ue()}
    if result['id'] > 255 or result['sps'] > 31:
        raise H264Error('invalid PPS reference')
    b.get(); result['bottom_poc'] = b.get()
    if b.ue():
        raise H264Error('slice groups/FMO are not supported')
    return result


def sei_messages(data):
    pos = 0
    while pos < len(data):
        if data[pos:] == b'\x80': return
        values = []
        for _ in range(2):
            value = 0
            while pos < len(data) and data[pos] == 255:
                value += 255; pos += 1
            if pos == len(data): raise H264Error('truncated SEI header')
            value += data[pos]; pos += 1
            values.append(value)
        kind, size = values
        if pos + size > len(data): raise H264Error('truncated SEI payload')
        yield kind, data[pos:pos + size]
        pos += size


def nals(reader, max_nals):
    # Stream in chunks; never buffer more than one bounded NAL plus one chunk.
    buffer = b''
    offset = count = 0
    start = re.compile(b'\x00\x00(?:\x00)?\x01')
    while offset < reader.size:
        amount = min(65536, reader.size - offset)
        buffer += reader.read(offset, amount); offset += amount
        while True:
            first = start.search(buffer)
            if first is None:
                if len(buffer) > 4: raise H264Error('missing Annex-B start code')
                break
            if any(buffer[:first.start()]): raise H264Error('nonzero bytes before start code')
            second = start.search(buffer, first.end())
            if second is None: break
            count += 1
            if count > max_nals: raise H264Error('NAL count limit exceeded')
            nal = buffer[first.end():second.start()].rstrip(b'\0')
            if not nal: raise H264Error('empty NAL unit')
            yield nal
            buffer = buffer[second.start():]
        if len(buffer) > 16 * 1024 * 1024:
            raise H264Error('NAL size limit exceeded')
    first = start.search(buffer)
    if first is None or any(buffer[:first.start()]): raise H264Error('invalid Annex-B ending')
    nal = buffer[first.end():].rstrip(b'\0')
    if not nal: raise H264Error('empty final NAL')
    if count + 1 > max_nals: raise H264Error('NAL count limit exceeded')
    yield nal


def measure(reader, max_frames):
    sets, pictures = {}, {}
    last_key = last_mb = None
    pending_timing = None
    boundary = False
    frames = slices = 0
    total = Fraction(0)
    clock = None
    unknown = None
    for nal in nals(reader, max_frames * 64 + 1024):
        if time.monotonic() >= reader.deadline: raise H264Error('scan deadline exceeded')
        header, kind = nal[0], nal[0] & 31
        if header & 128: raise H264Error('forbidden_zero_bit is set')
        # Only inspect the slice header, not its potentially huge compressed payload.
        data = rbsp(nal[1:65537] if kind in (1, 5) else nal[1:])
        if kind == 7:
            item = sps(data); sets[item['id']] = item; boundary = True
        elif kind == 8:
            item = pps(data); pictures[item['id']] = item; boundary = True
        elif kind == 6:
            boundary = True
            for tag, payload in sei_messages(data):
                if tag == 1:
                    if pending_timing is not None: raise H264Error('duplicate picture-timing SEI')
                    pending_timing = payload
        elif kind in (9, 10, 11):
            boundary = True
        elif kind == 12:
            pass  # filler data
        elif kind in (1, 5):
            slices += 1
            b = Bits(data)
            first_mb, slice_type, pid = b.ue(), b.ue(), b.ue()
            if slice_type > 9 or pid not in pictures:
                raise H264Error('invalid slice or missing PPS')
            pp = pictures[pid]
            if pp['sps'] not in sets: raise H264Error('missing active SPS')
            sp = sets[pp['sps']]
            if first_mb >= sp['mb_count']: raise H264Error('slice starts beyond picture')
            frame_num = b.get(sp['frame_bits'])
            idr = b.ue() if kind == 5 else None
            poc = ()
            if sp['poc_type'] == 0:
                poc = (b.get(sp['poc_bits']), b.se() if pp['bottom_poc'] else 0)
            elif sp['poc_type'] == 1 and not sp['delta_zero']:
                poc = (b.se(), b.se() if pp['bottom_poc'] else 0)
            key = (pid, frame_num, idr, bool(header & 96), poc)
            if first_mb:
                if boundary or key != last_key or last_mb is None or first_mb <= last_mb:
                    raise H264Error('missing/reordered first slice or unsupported slice order')
                last_mb = first_mb
                continue
            if last_key == key and not boundary:
                raise H264Error('duplicate first slice / ambiguous picture boundary')
            if not frames and kind != 5:
                raise H264Error('raw sequence does not start with an IDR picture')
            frames += 1
            if frames > max_frames: raise H264Error('scan frame limit exceeded')
            if sp['gaps_allowed']: unknown = 'frame_num gaps allowed; complete picture sequence not established'
            if not sp['timing']: unknown = 'active SPS has no VUI timing'
            elif not sp['fixed']: unknown = 'VUI clock is present but fixed_frame_rate_flag is false; variable timing not established'
            ticks = 2  # progressive frame, two clock ticks
            if sp['pic_struct']:
                if pending_timing is None:
                    unknown = 'pic_struct_present_flag requires picture-timing SEI for every picture'
                else:
                    timing = Bits(pending_timing)
                    timing.get(sp['hrd_bits'])
                    structure = timing.get(4)
                    if structure not in (0, 7, 8):
                        raise H264Error('field/repeated-field picture structure unsupported')
                    ticks = {0: 2, 7: 4, 8: 6}[structure]
                    for _ in range({0: 1, 7: 2, 8: 3}[structure]):
                        if timing.get():
                            raise H264Error('explicit SEI clock timestamps not implemented')
            if sp['timing']:
                if clock is not None and clock != sp['timing']:
                    unknown = 'active VUI clock changes within elementary stream'
                clock = sp['timing']
                total += Fraction(ticks * clock[0], clock[1])
            last_key, last_mb, boundary, pending_timing = key, 0, False, None
        else:
            raise H264Error('unsupported NAL type (partitioned/extended AVC): ' + str(kind))
    if not frames: raise H264Error('no complete picture headers')
    if pending_timing is not None: raise H264Error('picture-timing SEI without following picture')
    evidence = {'format': 'h264', 'pictures': frames, 'slices': slices,
                'method': 'progressive_picture_count_vui', 'timing_clock': clock}
    if unknown:
        return dict(evidence, status='unknown', reason=unknown)
    return dict(evidence, status='measured', duration_numerator=total.numerator,
                duration_denominator=total.denominator,
                duration_seconds=str(Decimal(total.numerator) / total.denominator))
