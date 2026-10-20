#!/usr/bin/env python3
# Copyright (c) 2026 aMule Team
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise connector config paths offline, with every output in a temporary directory."""
import configparser
import os
from pathlib import Path
import subprocess
import sys
import tempfile


PASSWORD = '0123456789abcdef0123456789abcdef'


def run(binary, root, *args, expected_error=None):
    env = dict(os.environ, HOME=str(root), XDG_CONFIG_HOME=str(root / 'xdg'))
    result = subprocess.run([binary, *args], cwd=root, env=env,
                            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, errors='replace', timeout=15)
    if expected_error is None:
        assert result.returncode == 0, (args, result.stdout)
    else:
        assert expected_error in result.stdout, (args, result.stdout)
    return result.stdout


def read_config(path):
    config = configparser.ConfigParser(interpolation=None)
    text = path.read_text()
    # wxFileConfig also writes ungrouped keys (Locale) before the first section.
    config.read_string(text[text.index('['):])
    return config


def check_path(binary, root, argument, expected):
    source = root / 'amule.conf'
    source.write_text(f'[ExternalConnect]\nECPort=48123\nECPassword={PASSWORD}\n')
    args = [] if argument is None else [f'--config-file={argument}']
    run(binary, root, *args, f'--create-config-from={source.as_posix()}')
    config = read_config(expected)
    assert config['EC']['Port'] == '48123', (expected, config.items('EC'))
    assert config['EC']['Password'].lower() == PASSWORD, expected
    # Read the same selected file, then save it without starting a connection.
    config['EC']['Host'] = 'config-path-test.invalid'
    with expected.open('w') as stream:
        config.write(stream)
    run(binary, root, *args, '--write-config', '--port=48124')
    config = read_config(expected)
    assert config['EC']['Port'] == '48124', expected
    assert config['EC']['Host'] == 'config-path-test.invalid', expected


def main():
    binaries = [str(Path(arg).resolve()) for arg in sys.argv[1:]]
    assert binaries, 'Pass amulecmd and/or amuleweb paths'
    with tempfile.TemporaryDirectory(prefix='amule-config-path-') as scratch:
        for index, binary in enumerate(binaries):
            base = Path(scratch).resolve() / str(index)
            # The portable marker prevents touching the real user config on Windows/macOS.
            cases = ['default', 'bare', 'absolute', 'dot-relative', 'parent-relative', 'subdirectory']
            if os.name == 'nt':
                cases += ['backslash', 'drive-relative', 'root-relative']
            if sys.platform.startswith('linux'):
                cases += ['home', 'user-default', 'user-bare']
            for case in cases:
                root = base / case
                root.mkdir(parents=True)
                portable = root / 'config'
                portable.mkdir()
                (portable / 'remote.conf').write_text('')
                expected = root / 'selected.conf'
                if case == 'default':
                    argument, expected = None, portable / 'remote.conf'
                elif case == 'bare':
                    argument, expected = 'custom.conf', portable / 'custom.conf'
                elif case == 'absolute':
                    argument = expected.as_posix()
                elif case == 'dot-relative':
                    argument = './selected.conf'
                elif case == 'parent-relative':
                    argument, expected = '../selected.conf', root.parent / 'selected.conf'
                elif case == 'subdirectory':
                    (root / 'subdir').mkdir()
                    argument, expected = 'subdir/selected.conf', root / 'subdir/selected.conf'
                elif case == 'backslash':
                    argument = str(expected)
                elif case == 'drive-relative':
                    argument = root.drive + 'selected.conf'
                elif case == 'root-relative':
                    argument = str(expected)[len(root.drive):]
                elif case == 'home':
                    argument = '~/selected.conf'
                else:
                    (portable / 'remote.conf').unlink()
                    argument = None if case == 'user-default' else 'custom.conf'
                    expected = root / '.aMule' / (argument or 'remote.conf')
                check_path(binary, root, argument, expected)
                print(f'{Path(binary).name}: {case} passed', flush=True)

            if Path(binary).stem == 'amuleweb':
                root = base / 'amule-config-file'
                root.mkdir()
                source = root / 'amule.conf'
                template = 'config-path-test-missing-template'
                source.write_text(f'[WebServer]\nTemplate={template}\n')
                # Stop at template validation, before any EC or HTTP connection is opened.
                for argument in [source.as_posix(), './amule.conf']:
                    run(binary, root, f'--amule-config-file={argument}',
                        expected_error=f'Cannot find template: {template}')
                print('amuleweb: amule-config-file passed', flush=True)


if __name__ == '__main__':
    main()
