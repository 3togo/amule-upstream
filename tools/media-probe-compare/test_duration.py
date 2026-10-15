import tempfile
import unittest
from pathlib import Path

from duration import scan
from compare import measured_input, normalize


def mp3_frame(info=False, count=2, delay=576, padding=128, rate_index=1):
    # MPEG-1 Layer III, no CRC, 128 kbps, mono.
    header = 0xfffb90c0 | (rate_index << 10)
    rate = (44100, 48000, 32000)[rate_index]
    data = bytearray(144000 * 128 // rate)
    data[:4] = header.to_bytes(4, 'big')
    if info:
        data[21:25] = b'Info'
        data[25:29] = (1).to_bytes(4, 'big')
        data[29:33] = count.to_bytes(4, 'big')
        data[33:42] = b'LAME3.100'
        data[54:57] = ((delay << 12) | padding).to_bytes(3, 'big')
    return bytes(data)


def adts_frame(rate_index=3):
    size = 10
    return bytes([255, 241, 64 | (rate_index << 2), 64 | (size >> 11),
                  (size >> 3) & 255, ((size & 7) << 5) | 31, 252, 0, 0, 0])


class DurationTests(unittest.TestCase):
    def measure(self, data, **kwargs):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'no-extension'
            path.write_bytes(data)
            return scan(path, **kwargs)

    def test_gapless_padding_is_subtracted_once(self):
        result = self.measure(mp3_frame(True) + mp3_frame() * 2)
        self.assertEqual(result['status'], 'measured')
        self.assertEqual(result['samples'], 2 * 1152 - 576 - 128)
        self.assertEqual(result['method'], 'gapless_frame_count')

    def test_no_xing_counts_all_frames(self):
        result = self.measure(mp3_frame() * 2)
        self.assertEqual(result['samples'], 2304)
        self.assertIsNone(result['encoder_padding'])

    def test_id3_is_not_audio(self):
        tag = b'ID3\x03\0\0\0\0\0\x04test'
        result = self.measure(tag + mp3_frame() * 2 + b'TAG' + bytes(125))
        self.assertEqual(result['samples'], 2304)

    def test_bad_padding_and_stale_frame_count_rejected(self):
        for info in (mp3_frame(True, count=3), mp3_frame(True, padding=3000)):
            self.assertEqual(self.measure(info + mp3_frame() * 2)['status'], 'unavailable')

    def test_truncation_and_rate_changes_rejected(self):
        for data in (mp3_frame()[:-1], mp3_frame() + mp3_frame(rate_index=0),
                     adts_frame()[:-1], adts_frame() + adts_frame(rate_index=4)):
            self.assertEqual(self.measure(data)['status'], 'unavailable')

    def test_adts_sample_count(self):
        result = self.measure(adts_frame() * 123)
        self.assertEqual(result['samples'], 125952)
        self.assertEqual(result['duration_seconds'], '2.624')

    def test_resource_limits(self):
        data = mp3_frame() * 2
        for limits in ({'max_bytes': 10}, {'max_frames': 1}, {'timeout': 0}):
            self.assertEqual(self.measure(data, **limits)['status'], 'unavailable')

    def test_unknown_h264_does_not_inherit_estimate(self):
        original = {'format': {'format_name': 'h264', 'duration': '2.36', 'bit_rate': '59963'},
                    'streams': [{'codec_name': 'h264', 'codec_type': 'video'}]}
        corrected, evidence = measured_input(original, {'status': 'unknown', 'format': 'h264', 'reason': 'no fixed timing'})
        self.assertEqual(evidence['status'], 'unknown')
        self.assertEqual(original['format']['duration'], '2.36')
        result = normalize(corrected, 17689, 'file-average')
        self.assertEqual((result['length_seconds'], result['bitrate_kbps'], result['codec']), (0, 0, 'h264'))

    def test_evidence_does_not_override_other_containers(self):
        original = {'format': {'format_name': 'mov,mp4,m4a,3gp,3g2,mj2', 'duration': '2.6'}}
        corrected, evidence = measured_input(original, {'status': 'measured', 'format': 'aac', 'duration_seconds': '2.624'})
        self.assertEqual(corrected, original)
        self.assertEqual(evidence['status'], 'unchanged')


if __name__ == '__main__':
    unittest.main()
