#!/usr/bin/env python3
# Copyright (c) 2026 aMule Team
# SPDX-License-Identifier: GPL-2.0-or-later
"""A loopback eMule peer's mod identity survives EC, REST and daemon restart."""
import argparse
import json
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time
import urllib.error
import urllib.request

from AllSearchIntegrationTest import C, EC
from DaemonConfig import write_amule_conf


def port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def wait_for(fn):
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        try:
            value = fn()
            if value:
                return value
        except (OSError, urllib.error.URLError):
            pass
        time.sleep(0.1)
    raise AssertionError('Timed out waiting for fixture state')


def run(daemon, api, root):
    ec_port, tcp_port, http_port = (port() for _ in range(3))
    daemon_dir, api_dir = root / 'daemon', root / 'api'
    daemon_dir.mkdir()
    api_dir.mkdir()
    write_amule_conf(daemon_dir, ec_port, Port=tcp_port, UDPEnable=0, FilterLanIPs=0)
    (daemon_dir / 'amule.conf').chmod(0o600)
    # Seed credits so the history is saved even though this test exchanges no file data.
    peer_hash = bytearray(range(16))
    peer_hash[5], peer_hash[14] = 14, 111
    peer_hash = bytes(peer_hash)
    credit = peer_hash + struct.pack('<IIIIIHB', 1234, 5678, int(time.time()), 0, 0, 0, 0) + bytes(80)
    assert len(credit) == 119
    (daemon_dir / 'clients.met').write_bytes(b'\x12' + struct.pack('<I', 1) + credit)
    config = api_dir / 'amuleapi.conf'
    config.touch(mode=0o600)
    config.write_text(f'[EC]\nHost=127.0.0.1\nPort={ec_port}\nPassword=regression\n'
                      f'[Server]\nBindAddress=127.0.0.1\nPort={http_port}\n')
    token = ''
    processes = []
    logs = []

    def launch(executable, directory, *args):
        log = (root / (executable.name + '.log')).open('a')
        logs.append(log)
        proc = subprocess.Popen([str(executable), '--config-dir', str(directory), *args],
                                stdout=log, stderr=log)
        processes.append(proc)
        return proc

    def stop(proc):
        proc.terminate()
        try:
            proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
            raise AssertionError('Fixture did not stop cleanly')
        assert proc.returncode == 0, proc.returncode
        processes.remove(proc)

    def request(path, body=None):
        headers = {'Content-Type': 'application/json'}
        if token:
            headers['Authorization'] = 'Bearer ' + token
        req = urllib.request.Request(f'http://127.0.0.1:{http_port}/api/v1' + path,
                                     headers=headers,
                                     data=None if body is None else json.dumps(body).encode())
        with urllib.request.urlopen(req, timeout=3) as reply:
            return json.load(reply)

    def ready():
        with socket.create_connection(('127.0.0.1', ec_port), timeout=0.2):
            return True

    def ec_history(mod):
        ec = EC(ec_port)
        try:
            op, tags = ec.call(C['EC_OP_GET_CLIENT_HISTORY'])
            assert op == C['EC_OP_CLIENT_HISTORY']
            key, fields = tags[C['EC_TAG_CLIENT']]
            assert key == peer_hash
            assert fields[C['EC_TAG_CLIENT_MOD_VERSION']][0].rstrip(b'\0').decode() == mod
            assert fields[C['EC_TAG_CLIENT_SOFT_VER_STR']][0].rstrip(b'\0').decode() == 'v0.70b'
        finally:
            ec.sock.close()

    def identity(rows, mod):
        return next((row for row in rows if row['user_hash'] == peer_hash.hex()
                     and row.get('client_mod_name') == mod
                     and row.get('software_version') == 'v0.70b'), None)

    try:
        core = launch(daemon, daemon_dir, '-o')
        wait_for(ready)
        subprocess.run([str(api), '--config-dir', str(api_dir), '--set-admin-pass', 'mod-test'],
                       check=True, stdout=subprocess.DEVNULL, timeout=20)
        web = launch(api, api_dir, '--foreground')
        wait_for(lambda: request('/health'))
        token = request('/auth/login?include_token=true', {'password': 'mod-test'})['token']
        # Load history before the hello to exercise reconciliation of a cached old record.
        request('/known_clients')
        for mod in ['AI v1.6', 'eMule Qt 1.0', '']:
            with socket.create_connection(('127.0.0.1', tcp_port), timeout=5) as peer:
                def string_tag(code, value):
                    data = value.encode()
                    return bytes([2, 1, 0, code]) + struct.pack('<H', len(data)) + data
                tags = string_tag(1, 'mod-history-peer')
                tags += bytes([3, 1, 0, 0xfb]) + struct.pack('<I', (70 << 10) | (1 << 7))
                tags += string_tag(0x55, mod)
                hello = b'\x10' + peer_hash + struct.pack('<IHI', 0x0100007f, tcp_port, 3) + tags + bytes(6)
                peer.sendall(b'\xe3' + struct.pack('<I', len(hello) + 1) + b'\x01' + hello)
                wait_for(lambda: identity(request('/clients')['clients'], mod or None))
                wait_for(lambda: identity(request('/known_clients')['known_clients'], mod or None))
                ec_history(mod)
            # Restart without any live peers: the response must come from clients.met.
            stop(web)
            stop(core)
            saved = (daemon_dir / 'clients.met').read_bytes()
            assert saved[124:133] == b'AMULEMD1\x02'
            core = launch(daemon, daemon_dir, '-o')
            wait_for(ready)
            ec_history(mod)
            web = launch(api, api_dir, '--foreground')
            wait_for(lambda: request('/health'))
            token = request('/auth/login?include_token=true', {'password': 'mod-test'})['token']
            row = wait_for(lambda: identity(request('/known_clients')['known_clients'], mod or None))
            assert row['uploaded_bytes_total'] == 1234 and row['downloaded_bytes_total'] == 5678
        print('PASS: live/cached/stored mod identity, bare version, EC, REST, restart and credits')
    except Exception:
        for log in logs:
            log.flush()
        for log in root.glob('*.log'):
            print(log.name + ':\n' + log.read_text()[-4000:])
        raise
    finally:
        for proc in reversed(processes[:]):
            stop(proc)
        for log in logs:
            log.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--amuled', type=Path, required=True)
    parser.add_argument('--amuleapi', type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='amule-mod-history-') as directory:
        run(args.amuled.resolve(), args.amuleapi.resolve(), Path(directory))
