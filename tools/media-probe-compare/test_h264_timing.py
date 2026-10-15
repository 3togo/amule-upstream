import tempfile
import unittest
from pathlib import Path

from duration import scan


class Writer:
    def __init__(self): self.bits = ''
    def u(self, value, width=1):
        self.bits += format(value, '0%db' % width)
        return self
    def ue(self, value):
        text = bin(value + 1)[2:]
        self.bits += '0' * (len(text) - 1) + text
        return self
    def finish(self):
        text = self.bits + '1'
        text += '0' * (-len(text) % 8)
        return int(text, 2).to_bytes(len(text) // 8, 'big')


def nal(kind, payload):
    escaped = bytearray()
    zeros = 0
    for byte in payload:
        if zeros >= 2 and byte <= 3:
            escaped.append(3); zeros = 0
        escaped.append(byte)
        zeros = zeros + 1 if byte == 0 else 0
    return b'\0\0\0\1' + bytes([kind]) + escaped


def sequence(tick=1, scale=50, fixed=True, timing=True, pic_struct=False):
    w = Writer().u(66, 8).u(0, 8).u(30, 8).ue(0)
    w.ue(0).ue(0).ue(0).ue(1).u(0).ue(3).ue(3).u(1).u(1).u(0).u(1)
    w.u(0).u(0).u(0).u(0).u(int(timing))
    if timing: w.u(tick, 32).u(scale, 32).u(int(fixed))
    w.u(0).u(0).u(int(pic_struct)).u(0)
    return nal(0x67, w.finish()) + nal(0x68, Writer().ue(0).ue(0).u(0).u(0).ue(0).finish())


def picture(index, first_mb=0):
    w = Writer().ue(first_mb).ue(2).ue(0).u(index % 16, 4)
    if not index: w.ue(0)
    w.u(index * 2 % 16, 4)
    return nal(0x65 if not index else 0x41, w.finish())


def timing_sei(structure):
    w = Writer().u(structure, 4)
    for _ in range({0: 1, 7: 2, 8: 3}[structure]): w.u(0)
    payload = w.finish()
    return nal(6, bytes([1, len(payload)]) + payload + b'\x80')


class H264Tests(unittest.TestCase):
    def measure(self, data, **limits):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'extensionless'
            path.write_bytes(data)
            return scan(path, **limits)

    def test_fixed_progressive_timing(self):
        r = self.measure(sequence() + b''.join(picture(i) for i in range(3)))
        self.assertEqual(r['status'], 'measured')
        self.assertEqual(r['duration_seconds'], '0.12')
        self.assertEqual(r['pictures'], 3)

    def test_multiple_slices_count_once(self):
        r = self.measure(sequence() + b''.join(picture(i) + picture(i, 8) for i in range(3)))
        self.assertEqual((r['pictures'], r['slices']), (3, 6))
        self.assertEqual(r['duration_seconds'], '0.12')

    def test_fractional_rate(self):
        r = self.measure(sequence(tick=1001, scale=60000) + b''.join(picture(i) for i in range(3)))
        self.assertEqual(r['duration_seconds'], '0.1001')

    def test_missing_or_variable_timing_is_unknown(self):
        for options in ({'timing': False}, {'fixed': False}):
            r = self.measure(sequence(**options) + picture(0))
            self.assertEqual(r['status'], 'unknown')
            self.assertNotIn('duration_seconds', r)

    def test_frame_doubling_and_tripling(self):
        data = sequence(pic_struct=True)
        for i, structure in enumerate((0, 7, 8)):
            data += timing_sei(structure) + picture(i)
        self.assertEqual(self.measure(data)['duration_seconds'], '0.24')

    def test_missing_picture_timing_is_unknown(self):
        r = self.measure(sequence(pic_struct=True) + picture(0))
        self.assertEqual(r['status'], 'unknown')

    def test_zero_clock_is_rejected(self):
        self.assertEqual(self.measure(sequence(scale=0) + picture(0))['status'], 'unavailable')

    def test_truncated_headers_and_missing_pps(self):
        for data in (nal(0x67, b'\x42'), sequence() + nal(0x65, b''), picture(0)):
            self.assertEqual(self.measure(data)['status'], 'unavailable')

    def test_orphan_timing_and_duplicate_slices(self):
        for data in (sequence() + picture(0) + timing_sei(0), sequence() + picture(0) * 2):
            self.assertEqual(self.measure(data)['status'], 'unavailable')

    def test_active_clock_change_is_unknown(self):
        data = sequence() + picture(0) + sequence(scale=60) + picture(1)
        self.assertEqual(self.measure(data)['status'], 'unknown')

    def test_frame_limit_is_enforced(self):
        r = self.measure(sequence() + picture(0) + picture(1), max_frames=1)
        self.assertEqual(r['status'], 'unavailable')


if __name__ == '__main__':
    unittest.main()
