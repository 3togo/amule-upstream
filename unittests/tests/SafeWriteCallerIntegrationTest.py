#!/usr/bin/env python3
# Copyright (c) 2026 aMule Team
# SPDX-License-Identifier: GPL-2.0-or-later
"""Force real daemon metadata commits to fail without altering host permissions."""
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile

from AllSearchIntegrationTest import C, connect_daemon, free_ports, retry_port_collisions
from DaemonConfig import write_amule_conf


def run(binary, fixture, mode):
    with tempfile.TemporaryDirectory(prefix='amule-safe-write-') as directory:
        root = Path(directory)
        (root / 'Incoming').mkdir()
        (root / 'Temp').mkdir()
        ec_port, peer_port = free_ports(2, peer_index=1)
        # Initialize the server-list writer without joining any network. Its only
        # saved server is loopback, and all shared offline download defaults stay.
        write_amule_conf(root, ec_port, Port=peer_port, UDPEnable=0, FilterLanIPs=0,
                         ConnectToED2K=1, Autoconnect=0)
        name = b'offline-save-regression'
        tag = b'\x02\x01\x00\x01' + struct.pack('<H', len(name)) + name
        server_before = (b'\xe0' + struct.pack('<I', 1) + socket.inet_aton('127.0.0.2')
                         + struct.pack('<HI', 4661, 1) + tag)
        server = root / 'server.met'
        server.write_bytes(server_before)
        known = root / 'known.met'
        known_before = b'\x0e' + struct.pack('<I', 0)
        known.write_bytes(known_before)
        if mode == 'backup':
            (root / 'server.met.bak').mkdir()
        env = dict(os.environ, HOME=str(root), XDG_CONFIG_HOME=str(root / 'xdg'),
                   LC_ALL='C.UTF-8', LD_PRELOAD=fixture)
        marker = root / 'promotion-blocked'
        if mode in ('server', 'known'):
            env['AMULE_TEST_SAFE_WRITE_TARGET'] = str(server if mode == 'server' else known) + '.new'
            env['AMULE_TEST_SAFE_WRITE_MARKER'] = str(marker)
        with (root / 'console.log').open('w') as console:
            proc = subprocess.Popen([binary, '-c', str(root), '--disable-fatal'], env=env,
                                    stdout=console, stderr=console)
            ec = None
            try:
                ec = connect_daemon(proc, ec_port, root, peer_port)
                ec.sock.sendall(struct.pack('!II', 0x20, 3)
                                + bytes([C['EC_OP_SHUTDOWN'], 0, 0]))
                assert proc.wait(timeout=30) == 0, 'daemon shutdown failed'
                text = (root / 'logfile').read_text(errors='replace')
                if mode in ('server', 'known'):
                    assert marker.exists(), 'promotion failure fixture was not exercised'
                if mode in ('server', 'backup'):
                    assert server.exists(), 'failed save removed the live server.met'
                    assert server.read_bytes() == server_before, 'failed save changed server.met'
                    if mode == 'server':
                        assert "IO failure while writing 'server.met'" in text or 'Failed to save server.met!' in text, 'server commit failure was not reported'
                    else:
                        assert "Cannot back up 'server.met'" in text or 'Failed to back up server.met' in text, 'server backup failure was not reported'
                elif mode == 'known':
                    assert known.read_bytes() == known_before, 'failed save changed known.met'
                    assert 'Cannot commit saved known files' in text, 'known-file commit failure was not reported'
                    assert 'finished saving known.met' not in text, 'failed commit reported success'
                else:
                    assert (root / 'server.met.bak').read_bytes() == server_before, 'backup did not retain old server list'
                    assert server.read_bytes() != server_before, 'normal save did not commit updated server metadata'
                    assert not (root / 'server.met.new').exists(), 'normal commit left a staging file'
                print(f'{mode}: metadata commit result and live-file preservation verified')
            except BaseException:
                console.flush()
                for filename in ('console.log', 'logfile'):
                    path = root / filename
                    if path.exists():
                        print(path.read_text(errors='replace')[-6000:], file=sys.stderr)
                raise
            finally:
                if ec:
                    ec.sock.close()
                if proc.poll() is None:
                    proc.terminate()
                    try:
                        proc.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.wait()


if __name__ == '__main__':
    for mode in ('success', 'server', 'backup', 'known'):
        retry_port_collisions(run, str(Path(sys.argv[1]).resolve()),
                              str(Path(sys.argv[2]).resolve()), mode)
