#!/usr/bin/env python3
# Copyright (c) 2026 aMule Team
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise automatic lookup widening through actual daemon UDP requests."""
from contextlib import ExitStack
import os
import fcntl
from pathlib import Path
import select
import socket
import struct
import subprocess
import sys
import tempfile
import time

from AllSearchIntegrationTest import C, connect_daemon, free_ports, retry_port_collisions, stop_daemon
from DaemonConfig import write_amule_conf


def owned_address():
    # Kad rejects RFC1918 and loopback routing contacts. Enumerate only owned
    # interface addresses; this test never sends a packet to a public peer.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        for _, name in socket.if_nameindex():
            try:
                data = fcntl.ioctl(probe.fileno(), 0x8915,
                                   struct.pack('256s', name.encode()[:15]))  # SIOCGIFADDR
                ip = socket.inet_ntoa(data[20:24])
            except OSError:
                continue
            octets = tuple(map(int, ip.split('.')))
            if (octets[0] in (10, 127) or octets[:2] == (192, 168)
                    or octets[:2] == (169, 254)
                    or (octets[0] == 172 and 16 <= octets[1] <= 31)):
                continue
            return ip
    return None


def run(binary, ip, widening):
    with tempfile.TemporaryDirectory(prefix='kad-lookup-scheduling-') as directory, ExitStack() as stack:
        root = Path(directory)
        peers = [stack.enter_context(socket.socket(socket.AF_INET, socket.SOCK_DGRAM))
                 for _ in range(7)]
        contacts = []
        identities = {}
        for number, peer in enumerate(peers, 1):
            peer.bind((ip, 0))
            port = peer.getsockname()[1]
            identities[port] = (0xA0000000, 0, 0, number)
            # Version 5 requests are plaintext, so no cryptographic stub is involved.
            contacts.append(struct.pack('<4IIHHB2IB', 0xA0000000, 0, 0, number,
                                        int.from_bytes(socket.inet_aton(ip), 'big'),
                                        port, port, 5, 0, 0, 1))
        (root / 'nodes.dat').write_bytes(struct.pack('<III', 0, 2, len(contacts))
                                        + b''.join(contacts))
        ec_port, tcp, udp = free_ports(3, udp_indices=(2,), peer_index=1)
        write_amule_conf(root, ec_port, Port=tcp, UDPPort=udp, ConnectToKad=1,
                         Autoconnect=0, FilterLanIPs=0,
                         sections={'Kad': {'ContactsPerIP': 7, 'ContactsPerSubnet': 7,
                                           'RequestTimeoutMs': 100, 'LateReplyGraceMs': 100,
                                           'WideningReasks': widening}})
        log = stack.enter_context((root / 'stdout.log').open('w'))
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / 'xdg'))
        proc = subprocess.Popen([binary, '-c', str(root)], env=env, stdout=log, stderr=log)
        ec = None
        try:
            ec = connect_daemon(proc, ec_port, root, tcp, udp)
            assert ec.call(C['EC_OP_KAD_START'])[0] == C['EC_OP_NOOP']
            ec.start('scheduling regression', kind=C['EC_SEARCH_KAD'])
            # The two closest seeds stay silent; farther peers return no referrals.
            # This triggers the automatic fallback independently of the UI reask path.
            ordinary = set()
            target = None
            silent = set()
            end = time.monotonic() + 12
            quiet_until = None
            while time.monotonic() < end:
                assert proc.poll() is None, 'daemon exited during lookup'
                readable, _, _ = select.select(peers, [], [], 0.1)
                for peer in readable:
                    packet, sender = peer.recvfrom(65535)
                    if packet[:2] != b'\xe4\x21' or len(packet) != 35:
                        continue
                    count = packet[2]
                    if target is None and count == 2:
                        target = packet[3:19]
                        chunks = struct.unpack('<4I', target)
                        silent = set(sorted(identities, key=lambda port: tuple(
                            a ^ b for a, b in zip(identities[port], chunks)))[:2])
                    if target is None or packet[3:19] != target:
                        continue
                    if count == 11:
                        assert widening != 0, 'WideningReasks=0 still sent an automatic wider query'
                        assert len(ordinary) >= 6, ordinary
                        return
                    assert count == 2, count
                    port = peer.getsockname()[1]
                    ordinary.add(port)
                    if port not in silent:
                        peer.sendto(b'\xe4\x29' + target + b'\x00', sender)
                if widening == 0 and len(ordinary) == len(peers):
                    if quiet_until is None:
                        quiet_until = time.monotonic() + 2
                    if time.monotonic() >= quiet_until:
                        return
            raise AssertionError(('lookup did not exercise dead-node fallback', widening, ordinary))
        except BaseException:
            log.flush()
            print((root / 'stdout.log').read_text(errors='replace'), file=sys.stderr)
            if (root / 'logfile').exists():
                print((root / 'logfile').read_text(errors='replace')[-8000:], file=sys.stderr)
            raise
        finally:
            if ec is not None:
                ec.sock.close()
            stop_daemon(proc)


if __name__ == '__main__':
    address = owned_address()
    if address is None:
        print('No owned Kad-compatible IPv4 address available')
        sys.exit(77)
    for allowance in (0, 1):
        retry_port_collisions(run, str(Path(sys.argv[1]).resolve()), address, allowance)
    print('Automatic lookup widening honors disabled and enabled policy values.')
