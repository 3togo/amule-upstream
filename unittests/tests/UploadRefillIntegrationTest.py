#!/usr/bin/env python3
# Copyright (c) 2026 aMule Team
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise real daemon uploads to isolated loopback peers and report whole-process cost.

No public servers, Kad peers, or user configuration. Requests default to a 24-block window;
received bytes are checked against the shared file. Linux /proc supplies CPU and context
switches for the daemon only. Example: python3 UploadRefillIntegrationTest.py build/src/amuled
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading
import time
import zlib

from AllSearchIntegrationTest import connect_daemon, exact, free_ports, retry_port_collisions, stop_daemon
from DaemonConfig import write_amule_conf
from ShareWatcherMoveIntegrationTest import shared

BLOCK = 184320


def send(sock, opcode, data=b''):
    sock.sendall(struct.pack('<BIB', 0xe3, len(data) + 1, opcode) + data)


def packet(sock):
    proto, size = struct.unpack('<BI', exact(sock, 5))
    data = exact(sock, size)
    return proto, data[0], data[1:]


def usage(pid):
    fields = Path(f'/proc/{pid}/stat').read_text().split(') ', 1)[1].split()
    cpu = (int(fields[11]) + int(fields[12])) / os.sysconf('SC_CLK_TCK')
    switches = 0
    for task in Path(f'/proc/{pid}/task').iterdir():
        try:
            for line in (task / 'status').read_text().splitlines():
                if line.startswith(('voluntary_ctxt_switches:', 'nonvoluntary_ctxt_switches:')):
                    switches += int(line.split()[1])
        except FileNotFoundError:
            pass
    return cpu, switches


def run(binary, seconds, slots, rate, compress=False, pause_ms=0, request_window=24,
        first_peer_block_bytes=BLOCK, separate_first_file=False, read_shim=None, warmup=2):
    with tempfile.TemporaryDirectory(prefix='amule-upload-refill-') as directory:
        root = Path(directory)
        incoming = root / 'Incoming'
        incoming.mkdir()
        (root / 'Temp').mkdir()
        content = (os.urandom(4096) * (64 * 1024 * 1024 // 4096) if compress
                   else os.urandom(64 * 1024 * 1024))
        filename = 'upload.txt' if compress else 'upload.bin'
        (incoming / filename).write_bytes(content)
        first_content = content
        first_filename = filename
        if separate_first_file:
            first_content = bytes([content[0] ^ 1]) + content[1:]
            first_filename = 'upload-slow.bin'
            (incoming / first_filename).write_bytes(first_content)
        ec_port, peer_port = free_ports(2, peer_index=1)
        write_amule_conf(root, ec_port, Port=peer_port, UDPEnable=0, FilterLanIPs=0,
                         MaxUpload=rate, SlotAllocation=10, MaxConnections=200,
                         IncomingDir=incoming)
        env = dict(os.environ, HOME=str(root), XDG_CONFIG_HOME=str(root / 'xdg'), LC_ALL='C.UTF-8')
        if read_shim:
            env['LD_PRELOAD'] = read_shim
        stop = threading.Event()
        ready = [threading.Event() for _ in range(slots)]
        received = [0] * slots
        compressed_packets = [0] * slots
        errors = []
        sockets = []
        threads = []
        with (root / 'console.log').open('w') as log:
            proc = subprocess.Popen([binary, '-c', str(root), '--disable-fatal'], env=env,
                                    stdout=log, stderr=log)
            ec = None
            try:
                ec = connect_daemon(proc, ec_port, root, peer_port)
                deadline = time.monotonic() + 30
                files = {}
                while filename not in files or first_filename not in files:
                    assert time.monotonic() < deadline, 'shared file was not hashed'
                    files = shared(ec)
                    time.sleep(0.1)

                def peer(index):
                    peer_content = first_content if index == 0 else content
                    file_hash = files[first_filename if index == 0 else filename]
                    block_bytes = first_peer_block_bytes if index == 0 else BLOCK
                    sock = socket.socket()
                    sockets.append(sock)
                    try:
                        address = f'127.0.0.{index + 2}'
                        sock.bind((address, 0))
                        sock.settimeout(40)
                        sock.connect(('127.0.0.1', peer_port))
                        name = f'refill-{index}'.encode()
                        tags = b'\x02\x01\x00\x01' + struct.pack('<H', len(name)) + name
                        if compress:
                            tags += (b'\x03\x01\x00\xfa' + struct.pack('<I', 1 << 20)
                                     + b'\x03\x01\x00\xfb' + struct.pack('<I', 50 << 10))
                        hello = (b'\x10' + hashlib.md5(name).digest() + socket.inet_aton(address)
                                 + struct.pack('<HI', sock.getsockname()[1], 3 if compress else 1) + tags + bytes(6))
                        send(sock, 0x01, hello)
                        while packet(sock)[1] != 0x4c:
                            pass
                        send(sock, 0x4f, file_hash)
                        send(sock, 0x54, file_hash)
                        while packet(sock)[1] != 0x55:
                            pass
                        pending = {}
                        packed = {}
                        next_offset = 0

                        def request():
                            nonlocal next_offset
                            while len(pending) < request_window:
                                starts, ends = [], []
                                for _ in range(min(3, request_window - len(pending))):
                                    if next_offset >= len(peer_content):
                                        next_offset = 0
                                    start = next_offset
                                    end = min(start + block_bytes, len(peer_content))
                                    starts.append(start)
                                    ends.append(end)
                                    pending[start] = [end, start]
                                    next_offset = end
                                starts += [0] * (3 - len(starts))
                                ends += [0] * (3 - len(ends))
                                send(sock, 0x47, file_hash + struct.pack('<6I', *starts, *ends))

                        request()
                        ready[index].set()
                        while not stop.is_set():
                            proto, opcode, data = packet(sock)
                            if (proto, opcode) not in ((0xe3, 0x46), (0xc5, 0x40)):
                                continue
                            assert data[:16] == file_hash
                            start, end = struct.unpack_from('<II', data, 16)
                            payload = data[24:]
                            if opcode == 0x40:
                                buffer = packed.setdefault(start, bytearray())
                                buffer.extend(payload)
                                if len(buffer) < end:
                                    continue
                                assert len(buffer) == end, 'compressed length differs'
                                payload = zlib.decompress(buffer)
                                compressed_packets[index] += 1
                                del packed[start]
                                end = start + len(payload)
                            assert payload == peer_content[start:end], 'upload payload differs'
                            if pause_ms and index == 0:
                                time.sleep(pause_ms / 1000)
                            received[index] += end - start
                            owner = next(key for key, value in pending.items() if key <= start < value[0])
                            assert pending[owner][1] == start, 'packet gap or duplicate'
                            pending[owner][1] = end
                            if end == pending[owner][0]:
                                del pending[owner]
                            if len(pending) <= request_window // 2:
                                request()
                    except BaseException as error:
                        if not stop.is_set():
                            errors.append(repr(error))
                    finally:
                        sock.close()

                for index in range(slots):
                    thread = threading.Thread(target=peer, args=(index,), daemon=True)
                    thread.start()
                    threads.append(thread)
                deadline = time.monotonic() + slots + 40
                while not all(event.is_set() for event in ready):
                    assert not errors, errors
                    assert time.monotonic() < deadline, 'upload slots did not start'
                    time.sleep(0.1)
                time.sleep(warmup)
                before = received.copy()
                cpu, switches = usage(proc.pid)
                start = time.monotonic()
                time.sleep(seconds)
                elapsed = time.monotonic() - start
                cpu_after, switches_after = usage(proc.pid)
                delivered = [after - old for after, old in zip(received, before)]
                assert not errors, errors
                assert all(count > 0 for count in delivered), 'starved upload slot'
                if compress:
                    assert sum(compressed_packets) > 0, 'compression was not exercised'
                mib = sum(delivered) / 1024**2
                return dict(seconds=elapsed, slots=slots, rate_kib=rate, compress=compress, pause_ms=pause_ms,
                            request_window=request_window, first_peer_block_bytes=first_peer_block_bytes,
                            separate_first_file=separate_first_file, read_shim=bool(read_shim), warmup=warmup,
                            compressed_packets=compressed_packets,
                            mib_per_second=mib / elapsed, cpu_ms_per_mib=1000 * (cpu_after - cpu) / mib,
                            context_switches_per_second=(switches_after - switches) / elapsed,
                            slot_kib_per_second=[count / 1024 / elapsed for count in delivered])
            except BaseException:
                print((root / 'console.log').read_text()[-5000:])
                raise
            finally:
                stop.set()
                for sock in sockets:
                    try:
                        sock.shutdown(socket.SHUT_RDWR)
                    except OSError:
                        pass
                for thread in threads:
                    thread.join(timeout=2)
                if ec:
                    ec.sock.close()
                stop_daemon(proc)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary')
    parser.add_argument('--seconds', type=float, default=5)
    parser.add_argument('--slots', type=int, default=3)
    parser.add_argument('--rate', type=int, default=2560, help='upload cap in KiB/s')
    parser.add_argument('--compress', action='store_true')
    parser.add_argument('--pause-ms', type=float, default=0, help='delay each packet on first peer')
    parser.add_argument('--request-window', type=int, default=24, help='pending ranges per peer')
    parser.add_argument('--first-peer-block-bytes', type=int, default=BLOCK,
                        help='range length for peer 0 (other peers use 184320 bytes)')
    parser.add_argument('--separate-first-file', action='store_true',
                        help='give peer 0 a distinct upload-slow.bin file')
    parser.add_argument('--read-shim', help='LD_PRELOAD library loaded only into the daemon')
    parser.add_argument('--warmup', type=float, default=2, help='seconds before measurement')
    args = parser.parse_args()
    if not 1 <= args.first_peer_block_bytes <= 3 * BLOCK:
        parser.error('--first-peer-block-bytes must be between 1 and 552960')
    if args.request_window < 3 or args.request_window * max(BLOCK, args.first_peer_block_bytes) >= 64 * 1024**2:
        parser.error('--request-window must be at least 3 and fit distinct ranges in the 64 MiB file')
    if args.warmup < 0:
        parser.error('--warmup must be nonnegative')
    print(json.dumps(retry_port_collisions(
        run, str(Path(args.binary).resolve()), args.seconds, args.slots, args.rate,
        args.compress, args.pause_ms, args.request_window, args.first_peer_block_bytes,
        args.separate_first_file, str(Path(args.read_shim).resolve()) if args.read_shim else None,
        args.warmup)))
