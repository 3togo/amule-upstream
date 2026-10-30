#!/usr/bin/env python3
# Copyright (c) 2026 aMule Team
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prove the daemon loads shared Kad settings from its actual configuration."""
import hashlib
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
from AllSearchIntegrationTest import C, connect_daemon, free_ports, retry_port_collisions


def run(binary):
    with tempfile.TemporaryDirectory(prefix='kad-constraints-') as directory:
        root = Path(directory)
        ec_port, tcp, udp = free_ports(3, udp_indices=(2,), peer_index=1)
        (root / 'amule.conf').write_text(f"""[eMule]
Nick=regression
Port={tcp}
UDPPort={udp}
ConnectToKad=1
Autoconnect=0
ConnectToED2K=0
NewVersionCheck=0
GeoIPEnabled=0
GeoIPAutoUpdate=0
UPnPEnabled=0
Reconnect=0
Serverlist=0
Ed2kServersUrl=
KadNodesUrl=
TempDir={root}/Temp
IncomingDir={root}/Incoming
[ExternalConnect]
AcceptExternalConnections=1
ECAddress=127.0.0.1
ECPort={ec_port}
ECPassword={hashlib.md5(b'regression').hexdigest()}
[Kad]
TrackedIPs=1
PingRequestsPerMinute=7
""")
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / 'xdg'))
        env.pop('LD_PRELOAD', None)
        with (root / 'stdout.log').open('w') as log:
            proc = subprocess.Popen([binary, '-c', str(root)], env=env, stdout=log, stderr=log)
            try:
                ec = connect_daemon(proc, ec_port, root, tcp, udp)
                assert ec.call(C['EC_OP_KAD_START'])[0] == C['EC_OP_NOOP']
                target = ('127.0.0.1', udp)
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as first, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as second:
                    first.bind(('127.0.0.1', 0))
                    second.bind(('127.0.0.2', 0))
                    first.settimeout(2)
                    # Seven replies prove that the configured rate replaces the default two.
                    for _ in range(7):
                        first.sendto(b'\xe4\x60', target)
                        data, _ = first.recvfrom(1024)
                        assert data[:2] == b'\xe4\x61', data
                    for peer in (first, second):
                        peer.settimeout(0.3)
                        peer.sendto(b'\xe4\x60', target)
                        try:
                            data, _ = peer.recvfrom(1024)
                        except socket.timeout:
                            pass
                        else:
                            raise AssertionError(('rate/IP capacity was not enforced', data))
                ec.sock.close()
            except BaseException:
                log.flush()
                print((root / 'stdout.log').read_text(errors='replace'), file=sys.stderr)
                raise
            finally:
                proc.terminate()
                try:
                    proc.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
    print('Configured Kad IP capacity and packet rate enforced by the real UDP listener.')


if __name__ == '__main__':
    retry_port_collisions(run, str(Path(sys.argv[1]).resolve()))
