"""The helper rejects repeated parser seeks, including a full-parse retry."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HELPER = str(Path(sys.argv.pop(1)).resolve())


class SeekLimitTests(unittest.TestCase):
    def test_seek_loop_is_a_file_verdict(self):
        for retry in (False, True):
            with self.subTest(full_parse_retry=retry), tempfile.TemporaryFile() as source:
                source.write(b'seek fixture')
                source.seek(0)
                env = dict(os.environ)
                env.pop('AMULE_TEST_SEEK_RETRY', None)
                if retry:
                    env['AMULE_TEST_SEEK_RETRY'] = '1'
                result = subprocess.run([HELPER], stdin=source, env=env,
                                        capture_output=True, timeout=3)
                self.assertEqual(result.returncode, 11, result.stderr)
                self.assertEqual(result.stdout, b'')


if __name__ == '__main__':
    unittest.main()
