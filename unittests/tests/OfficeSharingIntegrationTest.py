#!/usr/bin/env python3
# Copyright (c) 2026 aMule Team
# SPDX-License-Identifier: GPL-2.0-or-later
"""An existing profile gains the office exclusion default and keeps its opt-out."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
from AllSearchIntegrationTest import connect_daemon, free_ports, retry_port_collisions, stop_daemon
from DaemonConfig import write_amule_conf
from ShareWatcherMoveIntegrationTest import shared


def run(binary):
    with tempfile.TemporaryDirectory(prefix='amule-office-sharing-') as directory:
        root = Path(directory)
        incoming = root / 'Incoming'
        incoming.mkdir()
        (root / 'Temp').mkdir()
        names = {'normal.bin', 'report.DOCX', 'budget.xlsm', 'slides.odp', 'table.CSV', 'table.tsv'}
        for name in names:
            (incoming / name).write_bytes((name.encode() + b':') * 400)
        ports = free_ports(2, peer_index=1)
        write_amule_conf(root, ports[0], Port=ports[1], UDPEnable=0, IncomingDir=incoming,
                         ExcludeOfficeDocuments=0)
        env = dict(os.environ, HOME=str(root), XDG_CONFIG_HOME=str(root / 'xdg'), LC_ALL='C.UTF-8')
        env.pop('LD_PRELOAD', None)
        for phase, expected in [('disabled', names), ('existing-profile-default', {'normal.bin'}),
                                ('explicit-opt-out', names)]:
            config = root / 'amule.conf'
            if phase != 'disabled':
                text = re.sub(r'^ExcludeOfficeDocuments=.*\n', '', config.read_text(), flags=re.M)
                if phase == 'explicit-opt-out':
                    text = text.replace('[eMule]\n', '[eMule]\nExcludeOfficeDocuments=0\n', 1)
                config.write_text(text)
            ec = proc = None
            try:
                with (root / ('console-' + phase)).open('w') as log:
                    proc = subprocess.Popen([binary, '-c', str(root), '--disable-fatal'], env=env,
                                            stdout=log, stderr=log)
                ec = connect_daemon(proc, ports[0], root, ports[1])
                deadline = time.monotonic() + 15
                while True:
                    actual = set(shared(ec))
                    if actual == expected:
                        break
                    assert time.monotonic() < deadline, (phase, actual, expected)
                    time.sleep(0.05)
                assert all((incoming / name).exists() for name in names), 'sharing policy removed a file'
                print(phase, sorted(actual), flush=True)
            finally:
                if ec:
                    ec.sock.close()
                if proc:
                    stop_daemon(proc)


if __name__ == '__main__':
    retry_port_collisions(run, str(Path(sys.argv[1]).resolve()))
