#!/usr/bin/env python3
# Copyright (c) 2026 aMule Team
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise large-index handoff and restart/failure in an isolated real daemon."""
import hashlib
import os
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from AllSearchIntegrationTest import C, connect_daemon, free_port
binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='kad-index-review-') as directory:
    root = Path(directory)
    port = free_port()
    count = 50000
    with (root / 'load_index.dat').open('wb') as persisted:
        persisted.write(struct.pack('<III', 1, int(time.time()), count))
        for i in range(1, count + 1):
            persisted.write(bytes(12) + struct.pack('<II', i, int(time.time()) + 3600))
    (root / 'amule.conf').write_text(f"""[eMule]
Nick=review
Port={free_port()}
UDPPort={free_port()}
Address=127.0.0.1
ConnectToKad=1
Autoconnect=0
ConnectToED2K=0
NewVersionCheck=0
Reconnect=0
Serverlist=0
Ed2kServersUrl=
KadNodesUrl=
TempDir={root}/Temp
IncomingDir={root}/Incoming
[ExternalConnect]
AcceptExternalConnections=1
ECAddress=127.0.0.1
ECPort={port}
ECPassword={hashlib.md5(b'regression').hexdigest()}
""")
    with (root / 'stdout.log').open('w') as log:
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / 'xdg'))
        proc = subprocess.Popen([binary, '-c', str(root)], stdout=log, stderr=log, env=env)
        try:
            ec = connect_daemon(proc, port)
            assert ec.call(C['EC_OP_KAD_START'])[0] == C['EC_OP_NOOP']
            deadline = time.monotonic() + 15
            while True:
                _, tags = ec.call(C['EC_OP_STAT_REQ'])
                loaded = tags[C['EC_TAG_STATS_KAD_INDEXED_LOAD']][0]
                assert loaded in (0, count), 'partial index escaped the worker'
                if loaded == count:
                    break
                assert time.monotonic() < deadline, 'large index adoption timed out'
                time.sleep(0.05)
            assert ec.call(C['EC_OP_KAD_STOP'])[0] == C['EC_OP_NOOP']
            assert (root / 'key_index.dat').exists(), 'empty index was not adopted and saved'
            corrupt = struct.pack('<I', 4)
            (root / 'key_index.dat').write_bytes(corrupt)
            for i in range(10):
                assert ec.call(C['EC_OP_KAD_START'])[0] == C['EC_OP_NOOP']
                time.sleep(0.02 if i < 9 else 2)
                assert ec.call(C['EC_OP_KAD_STOP'])[0] == C['EC_OP_NOOP']
                assert (root / 'key_index.dat').read_bytes() == corrupt
                assert ec.call(C['EC_OP_STAT_REQ'])[0] == C['EC_OP_STATS']
                assert proc.poll() is None
            ec.sock.close()
            print('Real daemon: 50000-entry adoption/save and 10 corrupt-index start/stop cycles passed; files preserved and EC responsive.')
        except BaseException:
            log.flush()
            print((root / 'stdout.log').read_text(), file=sys.stderr)
            raise
        finally:
            if proc.poll() is None:
                proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
