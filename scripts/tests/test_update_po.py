"""Exercise catalog timestamp handling with real gettext tools."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "update-po.sh"
OLD_DATE = '"POT-Creation-Date: 2000-01-01 00:00+0000\\n"'
REVISION = '"PO-Revision-Date: 2001-02-03 04:05+0000\\n"'
SOURCE = '_("Alpha");\n_("Beta");\n'


class UpdatePoTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        (self.root / "po").mkdir()
        self.source = self.root / "source.cpp"
        self.source.write_text(SOURCE)
        (self.root / "po/POTFILES.in").write_text("source.cpp\n")
        self.pot = self.root / "po/amule.pot"
        self.po = self.root / "po/de.po"
        self.po.write_text(
            'msgid ""\nmsgstr ""\n'
            '"Content-Type: text/plain; charset=UTF-8\\n"\n'
            '"Language: de\\n"\n' + REVISION + '\n\n'
            'msgid "Alpha"\nmsgstr "Alfa"\n'
        )
        self.run_update()
        # A deliberately old date makes tests independent of wall-clock timing.
        for path in (self.pot, self.po):
            text = path.read_text()
            path.write_text(re.sub(r'^"POT-Creation-Date:.*$', lambda _: OLD_DATE,
                                   text, flags=re.MULTILINE))

    def run_update(self, success=True):
        result = subprocess.run(["bash", str(SCRIPT)], cwd=self.root,
                                capture_output=True, text=True)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(REVISION, self.po.read_text())
            self.assertIn('msgstr "Alfa"', self.po.read_text())
        else:
            self.assertNotEqual(result.returncode, 0)
        self.assertFalse(list((self.root / "po").glob(".update-po.*")))

    def date(self):
        return next(line for line in self.pot.read_text().splitlines()
                    if line.startswith('"POT-Creation-Date:'))

    def test_unchanged_regeneration_is_byte_identical(self):
        before = {p: p.read_bytes() for p in (self.pot, self.po)}
        self.run_update()
        for path, data in before.items():
            self.assertEqual(data, path.read_bytes())

    def test_references_order_and_copyright_update_without_new_date(self):
        self.source.unlink()
        (self.root / "moved.cpp").write_text('_("Beta");\n_("Alpha");\n')
        (self.root / "po/POTFILES.in").write_text("moved.cpp\n")
        self.pot.write_text(re.sub(r'Copyright \(C\) \d{4}', 'Copyright (C) 1999',
                                   self.pot.read_text()))
        self.run_update()
        self.assertEqual(OLD_DATE, self.date())
        for path in (self.pot, self.po):
            self.assertIn("#: moved.cpp", path.read_text())
            self.assertNotIn("#: source.cpp", path.read_text())
        self.assertNotIn("Copyright (C) 1999", self.pot.read_text())

    def test_content_changes_advance_date_then_stabilize(self):
        for source in (SOURCE + '_("Gamma");\n', '_("Alpha");\n',
                       SOURCE + 'wxPLURAL("one file", "%d files", n);\n',
                       '// TRANSLATORS: A letter.\n' + SOURCE):
            with self.subTest(source=source):
                self.source.write_text(source)
                self.pot.write_text(re.sub(r'^"POT-Creation-Date:.*$',
                                          lambda _: OLD_DATE, self.pot.read_text(),
                                          flags=re.MULTILINE))
                self.run_update()
                self.assertNotEqual(OLD_DATE, self.date())
                before = self.pot.read_bytes()
                self.run_update()
                self.assertEqual(before, self.pot.read_bytes())

    def test_context_and_format_flags_are_meaningful(self):
        for prefix in ('msgctxt "menu"\n', '#, c-format\n'):
            with self.subTest(prefix=prefix):
                text = self.pot.read_text().replace('msgid "Alpha"', prefix + 'msgid "Alpha"')
                self.pot.write_text(re.sub(r'^"POT-Creation-Date:.*$', lambda _: OLD_DATE,
                                          text, flags=re.MULTILINE))
                self.run_update()
                self.assertNotEqual(OLD_DATE, self.date())

    def test_failed_extraction_preserves_template(self):
        before = self.pot.read_bytes()
        (self.root / "po/POTFILES.in").write_text("missing.cpp\n")
        self.run_update(success=False)
        self.assertEqual(before, self.pot.read_bytes())


if __name__ == "__main__":
    unittest.main()
