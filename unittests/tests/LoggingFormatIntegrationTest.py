#!/usr/bin/env python3
# Copyright (c) 2026 aMule Team
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check stdout and legacy EC/logfile markers on an isolated offline daemon."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from AllSearchIntegrationTest import (C, connect_daemon, free_ports,
                                      retry_port_collisions, string, tag)
from DaemonConfig import write_amule_conf

STAMP = r'\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z'
# Legacy macros include a source basename and line number in Debug builds.
SOURCE = r'(?:[^()\r\n]+\(\d+\): )?'


def run(binary):
    with tempfile.TemporaryDirectory(prefix='amule-logging-format-') as directory:
        root = Path(directory)
        port, peer_port = free_ports(2)
        write_amule_conf(root, port, Port=peer_port, UDPEnable=0)
        env = dict(os.environ, HOME=str(root), XDG_CONFIG_HOME=str(root / 'xdg'),
                   LC_ALL='C.UTF-8')
        with (root / 'console.log').open('w') as console:
            proc = subprocess.Popen([binary, '-c', str(root), '--log-stdout', '--disable-fatal'],
                                    stdout=console, stderr=console, env=env)
            ec = None
            try:
                ec = connect_daemon(proc, port, root, peer_port)
                for status, message in [(False, 'logging-format-ordinary café'),
                                        (True, 'logging-format-status café')]:
                    tags = [string(C['EC_TAG_STRING'], message)]
                    if status:
                        tags.append(tag(C['EC_TAG_LOG_TO_STATUS']))
                    assert ec.call(C['EC_OP_ADDLOGLINE'], tags)[0] == C['EC_OP_NOOP']
                    op, tags = ec.call(C['EC_OP_GET_LOG'])
                    replay = tags[C['EC_TAG_STRING']][0].decode('utf-8')
                    assert op == C['EC_OP_LOG'], op
                    # C still means status-bar visibility, and retains INFO severity.
                    marker = '!' if status else ' '
                    pattern = re.compile(re.escape(marker) + STAMP
                                         + r' INFO \[core\] ' + SOURCE + re.escape(message))
                    disk = (root / 'logfile').read_text(encoding='utf-8')
                    match = next((line for line in disk.splitlines() if pattern.fullmatch(line)), None)
                    assert match is not None, disk
                    assert match in replay.splitlines(), replay
                    stdout = (root / 'console.log').read_text(encoding='utf-8')
                    assert match[1:] in stdout.splitlines(), stdout
                    assert match not in stdout.splitlines(), stdout
            except BaseException:
                console.flush()
                print((root / 'console.log').read_text(errors='replace'), file=sys.stderr)
                raise
            finally:
                if ec:
                    ec.sock.close()
                if proc.poll() is None:
                    proc.terminate()
                try:
                    proc.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
                assert proc.returncode == 0, proc.returncode
    print('LoggingFormatIntegrationTest: stdout, logfile and legacy EC replay passed')


if __name__ == '__main__':
    retry_port_collisions(lambda: run(str(Path(sys.argv[1]).resolve())))
