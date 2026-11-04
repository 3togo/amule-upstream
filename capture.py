#!/usr/bin/env python3
"""Capture unmodified PR #2347 with deterministic partial data and real local peers."""
import configparser
import ctypes
import json
import os
from pathlib import Path
import select
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

repo = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(repo / 'unittests/tests'))
from AllSearchIntegrationTest import EC, C, string
from DaemonConfig import write_amule_conf, daemon_env
from DownloadDisplayRatesIntegrationTest import hello, local_ipv4
from OfferFilesInteropPeer import md4
from UploadRefillIntegrationTest import packet, send

PART = 9728000
SIZE = 12 * PART - 1
NAME = b'partial-download.bin'
HASHES = b''.join(md4(bytes(PART if i < 11 else PART - 1)) for i in range(12))
HASH = md4(HASHES)
# Completed parts 0, 3, 7 and the first half of part 5: 29.2% complete.
GAPS = [(PART, 3 * PART), (4 * PART, 5 * PART),
        (5 * PART + PART // 2, 7 * PART), (8 * PART, SIZE)]
MAPS = [0x136, 0x31c, 0x530, 0x386]
# Missing part 11 has no source; other missing parts have differing availability.

def port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]

def seed(root):
    part = root / 'Temp/001.part'
    with part.open('wb') as f:
        f.truncate(SIZE)
    tags = [b'\x82\x01' + struct.pack('<H', len(NAME)) + NAME,
            b'\x83\x02' + struct.pack('<I', SIZE)]
    for i, (start, end) in enumerate(GAPS):
        for mark, value in ((9, start), (10, end)):
            key = bytes([mark]) + str(i).encode()
            tags.append(b'\x03' + struct.pack('<H', len(key)) + key + struct.pack('<I', value))
    (root / 'Temp/001.part.met').write_bytes(
        b'\xe0' + struct.pack('<I', int(part.stat().st_mtime)) + HASH
        + struct.pack('<H', 12) + HASHES + struct.pack('<I', len(tags)) + b''.join(tags))

def run(depth, display):
    root = Path(tempfile.mkdtemp(prefix=f'amule2347-depth-{depth}-'))
    for name in ('Incoming', 'Temp'):
        (root / name).mkdir()
    (root / 'lastversion').write_text('GIT\n')
    seed(root)
    ecport, peerport = port(), port()
    address = local_ipv4()
    write_amule_conf(root, ecport, Address=address, Port=peerport, UDPEnable=0,
                     FilterLanIPs=0, ConfirmExit=0, VerboseDebug=1, VerboseDebugLogfile=1, **{'3DDepth': depth},
                     sections={'GUI': {'StartupTab': 2},
                               'GUI/TransferWnd': {'ShowClientList': 1, 'Splitter': 290},
                               'Debug': {'Cat_Remote Client Protocol': 1, 'Cat_Local Client Protocol': 1, 'Cat_ED2k Client': 1, 'Cat_DownloadQueue': 1, 'Cat_IPFilter': 1}})
    env = daemon_env(root, DISPLAY=display, LANG='en_US.UTF-8')
    stopped = threading.Event()
    ready = [threading.Event() for _ in MAPS]
    errors, sockets, workers = [], [], []
    identities = []
    for _ in MAPS:
        s = socket.socket()
        s.bind((address, 0))
        identities.append(s)

    def serve(i):
        try:
            s = socket.socket()
            s.bind((address, 0))
            s.connect((address, peerport))
            s.settimeout(5)
            sockets.append(s)
            send(s, 1, hello(f'local-source-{i+1}'.encode(), address,
                             identities[i].getsockname()[1]))
            while not stopped.is_set():
                if not select.select([s], [], [], .1)[0]:
                    continue
                _, op, data = packet(s)
                if op == 0x4c:
                    send(s, 0x58, HASH)
                elif op == 0x58:
                    send(s, 0x59, HASH + struct.pack('<H', len(NAME)) + NAME)
                elif op in (0x4f, 0x59):
                    send(s, 0x50, HASH + struct.pack('<HH', 12, MAPS[i]))
                elif op == 0x51:
                    send(s, 0x52, HASH + struct.pack('<H', 12) + HASHES)
                elif op == 0x54:
                    send(s, 0x5c, struct.pack('<I', 10 + i))
                    ready[i].set()
        except Exception as e:
            if not stopped.is_set():
                errors.append(repr(e))

    with (root / 'console.log').open('w') as log:
        proc = subprocess.Popen([str(repo / 'build/src/amule'), '-c', str(root), '--disable-fatal'],
                                env=env, stdout=log, stderr=log)
        ec = None
        try:
            deadline = time.monotonic() + 45
            while ec is None:
                assert proc.poll() is None, (root / 'console.log').read_text()
                try:
                    ec = EC(ecport)
                except OSError:
                    assert time.monotonic() < deadline
                    time.sleep(.2)
            addresses = ','.join(f'{address}:{s.getsockname()[1]}' for s in identities)
            link = f'ed2k://|file|{NAME.decode()}|{SIZE}|{HASH.hex()}|sources,{addresses}|/'
            assert ec.call(C['EC_OP_ADD_LINK'], [string(C['EC_TAG_STRING'], link)])[0] == C['EC_OP_NOOP']
            for i in range(len(MAPS)):
                t = threading.Thread(target=serve, args=(i,), daemon=True)
                workers.append(t)
                t.start()
            deadline = time.monotonic() + 30
            while not all(r.is_set() for r in ready):
                assert not errors, errors
                assert time.monotonic() < deadline, (root / 'logfile').read_text()[-5000:]
                time.sleep(.1)
            # Select the download with native XTest input to populate its source list.
            x = ctypes.CDLL('libX11.so.6')
            xt = ctypes.CDLL('libXtst.so.6')
            x.XOpenDisplay.restype = ctypes.c_void_p
            d = x.XOpenDisplay(display.encode())
            x.XFlush.argtypes = [ctypes.c_void_p]
            x.XCloseDisplay.argtypes = [ctypes.c_void_p]
            xt.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
            xt.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
            xt.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
            xt.XTestFakeMotionEvent(d, -1, 160, 170, 0)
            xt.XTestFakeButtonEvent(d, 1, 1, 0)
            xt.XTestFakeButtonEvent(d, 1, 0, 0)
            xt.XTestFakeKeyEvent(d, 110, 1, 0)
            xt.XTestFakeKeyEvent(d, 110, 0, 0)
            xt.XTestFakeMotionEvent(d, -1, 1000, 300, 0)
            x.XFlush(d)
            time.sleep(2)
            response = ec.call_list(C['EC_OP_GET_DLOAD_QUEUE'])
            fields = {key: value for key, value, _ in response[1][0][2]}
            completed = int.from_bytes(fields[C['EC_TAG_PARTFILE_SIZE_DONE']], 'big')
            assert completed == SIZE - sum(b-a for a,b in GAPS), completed
            assert not errors, errors
            subprocess.run(['import', '-window', 'root', str(out / f'depth-{depth}.png')], env=env, check=True)
            x.XCloseDisplay(d)
        finally:
            stopped.set()
            for s in sockets + identities:
                s.close()
            for t in workers:
                t.join(timeout=1)
            if ec:
                ec.sock.close()
            proc.terminate()
            proc.wait(timeout=25)
    cfg = configparser.ConfigParser()
    cfg.read(root / 'amule.conf')
    assert cfg.getint('eMule', '3DDepth') == depth
    return {'depth': depth, 'completed_bytes': completed, 'size': SIZE,
            'source_maps': MAPS, 'profile': str(root), 'exit_code': proc.returncode}

readfd, writefd = os.pipe()
xvfb = subprocess.Popen(['Xvfb', '-displayfd', str(writefd), '-screen', '0', '1024x768x24'],
                        pass_fds=(writefd,), stderr=subprocess.DEVNULL)
os.close(writefd)
display = ':' + os.read(readfd, 32).decode().strip()
os.close(readfd)
try:
    results = []
    for depth in (0, 3, 4, 5):
        results.append(run(depth, display))
        print(json.dumps(results[-1]), flush=True)
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
finally:
    xvfb.terminate()
    xvfb.wait()
