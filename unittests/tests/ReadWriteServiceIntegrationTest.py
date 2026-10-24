#!/usr/bin/env python3
# Copyright (c) 2026 aMule Team
# SPDX-License-Identifier: GPL-2.0-or-later
"""A real upload read must wait behind a held download write on the same storage."""
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time
from AllSearchIntegrationTest import C, connect_daemon, free_ports, retry_port_collisions, stop_daemon
from DaemonConfig import write_amule_conf
from ED2KLinkIntegrationTest import add_link
from ShareWatcherMoveIntegrationTest import shared
from UploadStorageDelayIntegrationTest import BLOCK, Peer, wait_read
from WriteBackpressureIntegrationTest import Source, wait_for


def run(binary, reader, writer):
    with tempfile.TemporaryDirectory(prefix='amule-read-write-service-') as directory:
        root = Path(directory)
        incoming = root / 'Incoming'
        incoming.mkdir()
        (root / 'Temp').mkdir()
        content = os.urandom(2 * BLOCK)
        target = incoming / 'upload.bin'
        target.write_bytes(content)
        source = Source(0, False)
        ports = free_ports(2, peer_index=1)
        write_amule_conf(root, ports[0], Address=source.address, Port=ports[1], UDPEnable=0,
                         FilterLanIPs=0, MaxUpload=3500, MaxDownload=0, IncomingDir=incoming)
        write_gate = root / 'write-gate'
        write_gate.touch()
        arm = root / 'read-arm'
        notify_r, notify_w = os.pipe()
        release_r, release_w = os.pipe()
        preload = ':'.join(filter(None, [os.getenv('AMULE_TEST_PRELOAD_RUNTIME'), reader, writer]))
        env = dict(os.environ, HOME=str(root), XDG_CONFIG_HOME=str(root / 'xdg'), LC_ALL='C.UTF-8',
                   LD_PRELOAD=preload, AMULE_TEST_WRITE_GATE=str(write_gate),
                   AMULE_TEST_READ_ARM=str(arm), AMULE_TEST_READ_STAGE='read',
                   AMULE_TEST_READ_PREFIX=str(incoming), AMULE_TEST_READ_NOTIFY_FD=str(notify_w),
                   AMULE_TEST_READ_RELEASE_FD=str(release_r))
        proc = ec = peer = None
        try:
            with (root / 'console.log').open('w') as log:
                proc = subprocess.Popen([binary, '-c', str(root), '--disable-fatal'], env=env,
                                        stdout=log, stderr=log, pass_fds=(notify_w, release_r))
            os.close(notify_w)
            os.close(release_r)
            ec = connect_daemon(proc, ports[0], root, ports[1])
            files = {}
            def shares_ready():
                files.update(shared(ec))
                return 'upload.bin' in files
            wait_for(shares_ready, 20, lambda: files)
            peer = Peer(ports[1], 10, files['upload.bin'])
            assert add_link(ec, source.link()) == C['EC_OP_NOOP']
            source.start(ports[1])
            wait_for(lambda: Path(str(write_gate) + '.entered').exists(), 10,
                     lambda: (source.requested, source.errors))
            arm.touch()
            peer.request([(0, BLOCK)])
            peer.barrier()
            assert not select.select([notify_r], [], [], 0.5)[0], 'upload read overlapped held download write'
            ec.call(C['EC_OP_GET_DLOAD_QUEUE'])
            write_gate.unlink()
            wait_read(notify_r, 0)
            os.write(release_w, b'R')
            peer.receive((0, BLOCK), content)
            assert proc.poll() is None
            print('PASS: shared-storage upload waited behind the download write, then refilled with intact payload', flush=True)
        finally:
            write_gate.unlink(missing_ok=True)
            if arm.exists():
                os.write(release_w, b'R' * 8)
            if peer:
                peer.sock.close()
            if hasattr(source, 'worker'):
                source.close()
            else:
                source.identity.close()
            if ec:
                ec.sock.close()
            if proc:
                stop_daemon(proc)
            os.close(notify_r)
            os.close(release_w)


if __name__ == '__main__':
    retry_port_collisions(lambda: run(*[str(Path(p).resolve()) for p in sys.argv[1:4]]))
