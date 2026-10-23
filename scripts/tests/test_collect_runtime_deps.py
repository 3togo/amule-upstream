#!/usr/bin/env python3
"""Check dependency discovery without installing packages or running binaries."""
import os
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "collect-runtime-deps.sh"


class RuntimeDependenciesTest(unittest.TestCase):
    def run_collector(self, listing, owners, ldd_status=0, args=("binary",)):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "ldd").write_text(
                "#!/bin/bash\nprintf '%s\\n' \"$LISTING\"\nexit \"$LDD_STATUS\"\n"
            )
            (root / "dpkg-query").write_text(
                "#!/usr/bin/env python3\nimport json,os,sys\n"
                "owner=json.loads(os.environ['OWNERS']).get(sys.argv[2])\n"
                "if owner is None: sys.exit(1)\n"
                "print(owner + ': ' + sys.argv[2])\n"
            )
            for name in ("ldd", "dpkg-query"):
                (root / name).chmod(0o755)
            env = dict(os.environ, PATH=directory + os.pathsep + os.environ["PATH"],
                       LISTING=listing, OWNERS=json.dumps(owners), LDD_STATUS=str(ldd_status))
            return subprocess.run(["bash", str(SCRIPT), *args], env=env,
                                  capture_output=True, text=True)

    def test_transitive_libraries_loader_and_deduplication(self):
        result = self.run_collector(
            "linux-vdso.so.1 (0x1)\nliba.so => /lib/liba.so (0x2)\n"
            "libb.so => /lib/libb.so (0x3)\n/lib/ld-linux.so (0x4)",
            {"/lib/liba.so": "liba:amd64", "/lib/libb.so": "liba:amd64",
             "/lib/ld-linux.so": "libc6:amd64"}, args=("one", "two"))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "liba:amd64\nlibc6:amd64\n")

    def test_usr_merge_in_both_directions(self):
        result = self.run_collector(
            "liba.so => /lib/liba.so (0x1)\nlibb.so => /usr/lib/libb.so (0x2)",
            {"/usr/lib/liba.so": "liba:amd64", "/lib/libb.so": "libb:amd64"})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "liba:amd64\nlibb:amd64\n")

    def test_dpkg_diversion_records_are_not_packages(self):
        result = self.run_collector(
            "/lib/ld-linux.so (0x1)",
            {"/lib/ld-linux.so": "diversion by libc6 from: /lib/old-loader\nlibc6:amd64"})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "libc6:amd64\n")

    def test_missing_library_fails_without_partial_output(self):
        result = self.run_collector("liba.so => not found", {})
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")

    def test_unowned_library_fails(self):
        result = self.run_collector("liba.so => /opt/liba.so (0x1)", {})
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")

    def test_ldd_failure_and_empty_output_fail(self):
        for status in (0, 1):
            result = self.run_collector("", {}, ldd_status=status)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, "")

    def test_requires_binary(self):
        self.assertEqual(self.run_collector("", {}, args=()).returncode, 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
