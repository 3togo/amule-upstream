import unittest
from compare import normalize, mi_projection, number


class ContractTests(unittest.TestCase):
    def test_artwork_and_video_precedence(self):
        raw = {'streams': [
            {'codec_type': 'video', 'codec_name': 'mjpeg', 'disposition': {'attached_pic': 1}},
            {'codec_type': 'audio', 'codec_name': 'aac'},
            {'codec_type': 'video', 'codec_name': 'h264'}]}
        self.assertEqual(normalize(raw)['codec'], 'h264')
        raw['streams'].pop()
        self.assertEqual(normalize(raw)['codec'], 'aac')

    def test_restricted_stream_fallback(self):
        stream = {'codec_type': 'audio', 'codec_name': 'opus', 'tags': {'TITLE': 'Stream title'}}
        raw = {'streams': [stream]}
        self.assertEqual(normalize(raw)['title'], 'Stream title')
        raw['format'] = {'tags': {'Title': 'Container title'}}
        self.assertEqual(normalize(raw)['title'], 'Container title')
        raw.pop('format')
        stream['codec_name'] = 'aac'
        self.assertEqual(normalize(raw)['title'], '')
        stream['codec_name'] = 'opus'
        raw['streams'].append(dict(stream))
        self.assertEqual(normalize(raw)['title'], '')

    def test_success_and_rounding(self):
        self.assertEqual(number('2.5', rounded=True), 3)
        self.assertEqual(number('65999', 1000), 65)
        for invalid in ('N/A', None, '-1', 'NaN', 'Infinity'):
            self.assertEqual(number(invalid), 0)
        self.assertEqual(normalize({'format': {'tags': {'title': 'Only a tag'}}})['outcome'], 'NoUsableMetadata')
        self.assertEqual(normalize({'format': {'duration': '0.4'}})['outcome'], 'NoUsableMetadata')

    def test_tag_sanitization(self):
        result = normalize({'format': {'tags': {'ARTIST': ' a\nb\t ', 'album': 'x' * 300}}})
        self.assertEqual(result['artist'], 'ab')
        self.assertEqual(len(result['album']), 256)

    def test_unmapped_codec_is_visible(self):
        projection, warnings = mi_projection({'media': {'track': [{'@type': 'Audio', 'Format': 'Future codec'}]}})
        self.assertTrue(warnings)
        self.assertEqual(normalize(projection)['codec'], 'UNMAPPED:Future codec')

    def test_wave_mapping(self):
        projected, warnings = mi_projection({'media': {'track': [
            {'@type': 'General', 'Format': 'Wave', 'Director': 'Artist', 'OriginalSourceForm_Name': 'Album'},
            {'@type': 'Audio', 'Format': 'PCM', 'BitDepth': '16',
             'Format_Settings_Endianness': 'Little', 'Format_Settings_Sign': 'Signed'}]}})
        result = normalize(projected)
        self.assertFalse(warnings)
        self.assertEqual((result['artist'], result['album'], result['codec']), ('Artist', 'Album', 'pcm_s16le'))

    def test_file_average_uses_unrounded_duration(self):
        raw = {'format': {'duration': '2.6', 'bit_rate': 64000}}
        self.assertEqual(normalize(raw)['bitrate_kbps'], 64)
        self.assertEqual(normalize(raw, 21424, 'file-average')['bitrate_kbps'], 65)
        raw['format']['duration'] = None
        self.assertEqual(normalize(raw, 21424, 'file-average')['bitrate_kbps'], 0)

    def test_native_title_preserves_literal_slash_and_backslash(self):
        projected, _ = mi_projection({'media': {'track': [
            {'@type': 'General', 'Title': {'@dt': 'binary.base64', '#value': 'unused'},
             '_native_tags': {'Title': 'one\ntwo / literal \\n |SC1|'}}]}})
        self.assertEqual(normalize(projected)['title'], 'onetwo / literal \\n |SC1|')


if __name__ == '__main__':
    unittest.main()
