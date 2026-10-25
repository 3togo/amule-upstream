#!/usr/bin/env python3
# Copyright (c) 2026 aMule Team
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tiny packets and a held partial write must still finish one correct file."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
from AllSearchIntegrationTest import C, connect_daemon, free_ports, retry_port_collisions, stop_daemon
from CancelCompletedIntegrationTest import md4
from DaemonConfig import write_amule_conf
from ED2KLinkIntegrationTest import add_link
from UploadRefillIntegrationTest import send
from WriteBackpressureIntegrationTest import Source, wait_for
import struct


class TinySource(Source):
    def __init__(self):
        super().__init__(0, False, None)
        self.name = b'tiny-fragments.bin'
        self.size = 512 * 1024
        self.file_hash = md4(b'x' * self.size)
        self.hashes = b''
        self.prefix_sent = threading.Event()
        self.send_tail = threading.Event()
        self.sent_ranges = {}

    def link(self):
        return (f'ed2k://|file|{self.name.decode()}|{self.size}|{self.file_hash.hex()}|'
                f'sources,{self.address}:{self.identity.getsockname()[1]}|/')

    def send_block(self, peer, start, end):
        if not self.prefix_sent.is_set():
            packets = []
            for offset in range(start, min(end, start + 8192)):
                payload = self.file_hash + struct.pack('<II', offset, offset + 1) + b'x'
                packets.append(struct.pack('<BIB', 0xe3, len(payload) + 1, 0x46) + payload)
            last = min(end, start + 65536)
            payload = self.file_hash + struct.pack('<II', start + 8192, last) + b'x' * (last - start - 8192)
            packets.append(struct.pack('<BIB', 0xe3, len(payload) + 1, 0x46) + payload)
            peer.sendall(b''.join(packets))
            self.prefix_sent.set()
            assert self.send_tail.wait(15), 'partial-write checkpoint did not release the source'
        # Resending the prefix also checks that overlap does not change its
        # immutable snapshot or create another payload allocation.
        send(peer, 0x46, self.file_hash + struct.pack('<II', start, end) + b'x' * (end - start))
        self.sent_ranges[start] = end
        if sum(last - first for first, last in self.sent_ranges.items()) == self.size:
            # A reply on this same socket proves the core processed preceding
            # payload packets while its first disk write is still held. The
            # deliberately unknown file ID always receives a no-file reply.
            send(peer, 0x4f, bytes(16))


def run(binary, shim):
    with tempfile.TemporaryDirectory(prefix='amule-tiny-write-') as directory:
        root = Path(directory)
        (root / 'Temp').mkdir()
        (root / 'Incoming').mkdir()
        source = TinySource()
        ports = free_ports(2, peer_index=1)
        write_amule_conf(root, ports[0], Address=source.address, Port=ports[1], UDPEnable=0,
                         FilterLanIPs=0, MaxUpload=0, MaxDownload=0, FileBufferSizePref=1)
        gate = root / 'held-write'
        gate.touch()
        preload = ':'.join(filter(None, [os.getenv('AMULE_TEST_PRELOAD_RUNTIME'), shim]))
        env = dict(os.environ, HOME=str(root), XDG_CONFIG_HOME=str(root / 'xdg'), LC_ALL='C.UTF-8',
                   LD_PRELOAD=preload, AMULE_TEST_WRITE_GATE=str(gate))
        proc = ec = None
        try:
            with (root / 'console.log').open('w') as log:
                proc = subprocess.Popen([binary, '-c', str(root), '--disable-fatal'], env=env,
                                        stdout=log, stderr=log)
            ec = connect_daemon(proc, ports[0], root, ports[1])
            assert add_link(ec, source.link()) == C['EC_OP_NOOP']
            source.start(ports[1])
            detail = lambda: (source.requested, source.errors,
                              (root / 'logfile').read_text(errors='replace')[-3000:])
            wait_for(lambda: source.prefix_sent.is_set() and Path(str(gate) + '.entered').exists(),
                     12, detail)
            assert not source.cancelled and not source.errors, detail()
            source.send_tail.set()
            wait_for(lambda: source.requested == source.size, 5, detail)
            wait_for(lambda: '0x48' in source.opcodes, 8,
                     lambda: ('tail packets were not processed while the writer was held', detail()))
            ec.call(C['EC_OP_GET_DLOAD_QUEUE'])
            gate.unlink()
            completed = root / 'Incoming' / source.name.decode()
            wait_for(completed.exists, 15, detail)
            assert completed.read_bytes() == b'x' * source.size
            assert source.requested == source.size, ('premature hashing caused a re-download', detail())
            assert not source.errors, detail()
            print('PASS: 8,192 one-byte packets, overlap and concurrent snapshot reception completed '
                  'with intact payload and no re-download', flush=True)
        finally:
            source.send_tail.set()
            gate.unlink(missing_ok=True)
            if hasattr(source, 'worker'):
                source.close()
            else:
                source.identity.close()
            if ec:
                ec.sock.close()
            if proc:
                stop_daemon(proc)


if __name__ == '__main__':
    retry_port_collisions(lambda: run(*[str(Path(p).resolve()) for p in sys.argv[1:3]]))
