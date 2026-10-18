#!/usr/bin/env python3
# Copyright (c) 2026 aMule Team
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise shared addresses.dat updates through local HTTP, EC and amuled."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json
import os
import struct
import subprocess
import sys
import tempfile
import threading
import time
from urllib.error import URLError, HTTPError
from urllib.request import ProxyHandler, Request, build_opener

from AllSearchIntegrationTest import C, free_ports, stop_daemon, string, tag, integer, retry_port_collisions
from DaemonConfig import EC_PASSWORD, write_amule_conf
from KnownHistoryIntegrationTest import start

SERVER_MET = b'\xe0' + struct.pack('<I', 1) + bytes([203, 0, 113, 7]) + struct.pack('<HI', 4661, 0)
EXISTING = SERVER_MET.replace(bytes([203, 0, 113, 7]), bytes([203, 0, 113, 9]))
TRUNCATED = b'\xe0' + struct.pack('<I', 2) + bytes([203, 0, 113, 8]) + struct.pack('<HI', 4661, 0)
GOOD = (200, SERVER_MET)
SECOND = (200, SERVER_MET.replace(bytes([203, 0, 113, 7]), bytes([203, 0, 113, 10])))
FAIL = (503, b'unavailable')


def wait_for(proc, predicate, root):
    deadline = time.monotonic() + 20
    while not predicate():
        if proc.poll() is not None or time.monotonic() > deadline:
            raise AssertionError((root / 'logfile').read_text(errors='replace'))
        time.sleep(0.05)


def api_post(endpoint, path, payload, token=None):
    headers = {'Content-Type': 'application/json', 'Accept': 'application/jwt'}
    if token:
        headers['Authorization'] = 'Bearer ' + token
    req = Request(endpoint + path, json.dumps(payload).encode(), headers)
    with build_opener(ProxyHandler({})).open(req, timeout=2) as response:
        return response.status, response.read()


def start_api(binary, root, ec_port):
    root.mkdir()
    port = free_ports(1)[0]
    config = root / 'amuleapi.conf'
    config.write_text(f'[Server]\nBindAddress=127.0.0.1\nPort={port}\n'
                      f'[EC]\nHost=127.0.0.1\nPort={ec_port}\nPassword={EC_PASSWORD}\n')
    config.chmod(0o600)
    env = dict(os.environ, HOME=str(root), LC_ALL='C.UTF-8', LANG='C.UTF-8')
    with (root / 'console.log').open('w') as output:
        subprocess.run([binary, '--config-dir=' + str(root), '--set-admin-pass=fallback-test'],
                       env=env, stdout=output, stderr=subprocess.STDOUT, timeout=15, check=True)
        proc = subprocess.Popen([binary, '--config-dir=' + str(root), '--foreground', '--no-log-file'],
                                env=env, stdout=output, stderr=subprocess.STDOUT)
    endpoint = f'http://127.0.0.1:{port}/api/v1'
    try:
        deadline = time.monotonic() + 20
        while True:
            try:
                status, data = api_post(endpoint, '/auth/login', {'password': 'fallback-test'})
                assert status == 200, data
                return proc, endpoint, json.loads(data)['token']
            except (URLError, HTTPError):
                if proc.poll() is not None or time.monotonic() > deadline:
                    raise AssertionError((root / 'console.log').read_text(errors='replace'))
                time.sleep(0.05)
    except BaseException:
        proc.terminate()
        proc.wait(timeout=10)
        raise


def run(binary, root, responses, sources, expected, *, url=None, startup=False,
        success=True, reload=False, overlap=False, skipped=False, cancel=False,
        automatic=False, api_binary=None, legacy='', missing=False):
    requests = []
    release = threading.Event()

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            requests.append(self.path)
            if (overlap or cancel) and self.path == expected[0]:
                release.wait(timeout=20)
            status, body = responses.get(self.path, FAIL)
            self.send_response(status)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                assert cancel

        def log_message(self, *_args):
            pass

    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    proc = ec = api_proc = None
    try:
        root.mkdir()
        (root / 'Incoming').mkdir()
        (root / 'Temp').mkdir()
        if not startup:
            (root / 'server.met').write_bytes(EXISTING)
        ports = free_ports(3, udp_indices=(2,), peer_index=1)
        base = f'http://127.0.0.1:{server.server_port}'
        source_file = root / 'addresses.dat'

        def source_text(paths):
            return ''.join((base + entry if entry.startswith('/') else entry) + '\n' for entry in paths)

        if sources is not None and not reload:
            source_file.write_text(source_text(sources))
        requested = base + url if url and url.startswith('/') else url
        legacy_url = base + legacy if legacy.startswith('/') else legacy
        write_amule_conf(root, ports[0], Port=ports[1], UDPPort=ports[2], ConnectToED2K=1,
                         Ed2kServersUrl=legacy_url, Serverlist=int(automatic))
        proc, ec = start(binary, root, ports)
        if reload:
            source_file.write_text(source_text(sources))
        if missing:
            source_file.unlink()
        expected_source_file = source_file.read_bytes() if source_file.exists() else None
        if api_binary:
            api_proc, endpoint, token = start_api(api_binary, root / 'api', ports[0])
            status, data = api_post(endpoint, '/servers/update', {} if requested is None else {'url': requested}, token)
            assert status == 202, (status, data)
        elif not startup and not automatic:
            tags = [] if requested is None else [string(C['EC_TAG_SERVERS_UPDATE_URL'], requested)]
            op, _ = ec.call(C['EC_OP_SERVER_UPDATE_FROM_URL'], tags)
            assert op == C['EC_OP_NOOP'], op
        if overlap:
            wait_for(proc, lambda: len(requests) == 1, root)
            # The current sequence must retain its snapshot despite live edits and another request.
            source_file.write_text(source_text(['/changed']))
            expected_source_file = source_file.read_bytes()
            op, _ = ec.call(C['EC_OP_SERVER_UPDATE_FROM_URL'],
                            [string(C['EC_TAG_SERVERS_UPDATE_URL'], base + '/overlap')])
            assert op == C['EC_OP_NOOP'], op
            wait_for(proc, lambda: 'A server list download is already in progress.' in
                     (root / 'logfile').read_text(errors='replace'), root)
            release.set()
        if cancel:
            wait_for(proc, lambda: len(requests) == 1, root)
            stop_daemon(proc)
            release.set()
        else:
            wait_for(proc, lambda: len(requests) >= len(expected), root)
            if expected:
                last_url = base + expected[-1]
                wait_for(proc, lambda: last_url in (root / 'logfile').read_text(errors='replace') and
                         (('Finished downloading the server list from ' + last_url) in
                          (root / 'logfile').read_text(errors='replace') or
                          ('Failed to download server.met from ' + last_url) in
                          (root / 'logfile').read_text(errors='replace') or skipped), root)
            time.sleep(0.3)
        assert requests == expected, requests
        data = (root / 'server.met').read_bytes() if (root / 'server.met').exists() else b''
        assert bytes([203, 0, 113, 8]) not in data, data
        if not startup:
            assert bytes([203, 0, 113, 9]) in data, data
        if success:
            assert bytes([203, 0, 113, 7]) in data, data
            expected_ips = {9} if not startup else set()
            for path in expected:
                status, body = responses.get(path, FAIL)
                if status == 200 and body in (GOOD[1], SECOND[1]):
                    expected_ips.add(body[8])
            assert int.from_bytes(data[1:5], 'little') == len(expected_ips), (expected_ips, data)
            for last in expected_ips:
                assert bytes([203, 0, 113, last]) in data, data
        assert (source_file.read_bytes() if source_file.exists() else None) == expected_source_file
        # A one-off request must never reintroduce the retired config key.
        assert 'Ed2kServersUrl=' not in (root / 'amule.conf').read_text()
    finally:
        release.set()
        if api_proc:
            api_proc.terminate()
            api_proc.wait(timeout=10)
        if ec:
            ec.sock.close()
        if proc and proc.poll() is None:
            stop_daemon(proc)
        server.shutdown()
        server.server_close()
        worker.join(timeout=5)


def editor_case(binary, api_binary, root):
    root.mkdir()
    (root / 'Incoming').mkdir()
    (root / 'Temp').mkdir()
    (root / 'server.met').write_bytes(EXISTING)
    source_file = root / 'addresses.dat'
    original = '# Keep comments during migration of an uncustomised default.\nhttps://one.example/list\n'
    source_file.write_text(original)
    ports = free_ports(3, udp_indices=(2,), peer_index=1)
    write_amule_conf(root, ports[0], Port=ports[1], UDPPort=ports[2],
                     Ed2kServersUrl='https://upd.emule-security.org/server.met')
    proc, ec = start(binary, root, ports)
    api_proc = None
    try:
        assert source_file.read_text() == original
        api_proc, endpoint, token = start_api(api_binary, root / 'api', ports[0])

        def request(method, path, body=None, expected=200):
            data = None if body is None else json.dumps(body).encode()
            req = Request(endpoint + path, data, {'Authorization': 'Bearer ' + token,
                          'Content-Type': 'application/json'}, method=method)
            try:
                with build_opener(ProxyHandler({})).open(req, timeout=3) as response:
                    assert response.status == expected, response.status
                    result = response.read()
                    return json.loads(result) if result else None
            except HTTPError as error:
                assert error.code == expected, (error.code, error.read())

        def prefs():
            return request('GET', '/preferences')['servers']

        urls = ['https://one.example/list', 'http://two.example/list']
        request('PATCH', '/preferences', {'servers': {'update_urls': urls}})
        wait_for(proc, lambda: prefs()['update_urls'] == urls, root)
        assert prefs()['update_url'] == urls[0]
        assert source_file.read_text().splitlines() == urls
        request('PATCH', '/preferences', {'servers': {'update_url': 'http://legacy.example/list'}})
        expected = ['http://legacy.example/list', urls[1]]
        wait_for(proc, lambda: prefs()['update_urls'] == expected, root)
        for invalid in ['not-an-array', [17], ['ftp://bad/list'], ['http://host/list\nhttp://other/list'],
                        ['http://host/a\x00hidden']]:
            request('PATCH', '/preferences', {'servers': {'update_urls': invalid}}, expected=400)
            assert source_file.read_text().splitlines() == expected
        # Exercise the actual EC representation, including duplicate child names and >255 entries.
        many = [f'https://source.example/{i}' for i in range(300)]
        listing = tag(C['EC_TAG_SERVERS_URL_LIST'], children=[string(C['EC_TAG_STRING'], u) for u in many])
        op, result = ec.call(C['EC_OP_SET_PREFERENCES'],
                            [tag(C['EC_TAG_PREFS_SERVERS'], children=[listing])])
        assert op == C['EC_OP_NOOP'], (op, result)
        assert source_file.read_text().splitlines() == many
        wait_for(proc, lambda: prefs()['update_urls'] == many, root)
        # Invalid EC input is rejected without truncating the existing file.
        bad = tag(C['EC_TAG_SERVERS_URL_LIST'], children=[integer(C['EC_TAG_STRING'], 3)])
        op, _ = ec.call(C['EC_OP_SET_PREFERENCES'], [tag(C['EC_TAG_PREFS_SERVERS'], children=[bad])])
        assert op == C['EC_OP_FAILED'], op
        assert source_file.read_text().splitlines() == many
        for invalid in ['', 'http:///missing-host', 'http://host/a\x00hidden', 'http://host/list\nother']:
            request('POST', '/servers/update', {'url': invalid}, expected=400)
        op, _ = ec.call(C['EC_OP_SERVER_UPDATE_FROM_URL'], [integer(C['EC_TAG_SERVERS_UPDATE_URL'], 1)])
        assert op == C['EC_OP_FAILED'], op
        # A write failure is reported to API clients, preserving the existing filesystem object.
        source_file.unlink()
        source_file.mkdir()
        request('PATCH', '/preferences', {'servers': {'update_urls': urls}}, expected=400)
        assert source_file.is_dir()
        source_file.rmdir()
        source_file.write_text('\n'.join(many) + '\n')
        request('PATCH', '/preferences', {'servers': {'update_urls': []}})
        wait_for(proc, lambda: prefs()['update_urls'] == [], root)
        assert prefs()['update_url'] == ''
        assert source_file.read_text() == ''
        # The empty list remains empty after restarting, with no legacy key to remigrate.
        ec.sock.close()
        ec = None
        stop_daemon(proc)
        proc, ec = start(binary, root, ports)
        assert source_file.read_text() == ''
    finally:
        if api_proc:
            api_proc.terminate()
            api_proc.wait(timeout=10)
        if ec:
            ec.sock.close()
        if proc.poll() is None:
            stop_daemon(proc)


def migration_cases(binary, root):
    default = 'https://upd.emule-security.org/server.met'
    cases = [
        ('fresh-default', None, None, default + '\n'),
        ('keep-empty', default, '', ''),
        ('keep-comments', default, '# custom sources\nhttp://existing.example/list\n',
         '# custom sources\nhttp://existing.example/list\n'),
        ('custom-first', 'https://custom.example/list', 'http://existing.example/list\n',
         'https://custom.example/list\nhttp://existing.example/list\n'),
        ('custom-deduplicated', 'https://custom.example/list',
         'http://existing.example/list\nhttps://custom.example/list\n',
         'https://custom.example/list\nhttp://existing.example/list\n'),
    ]
    for name, legacy, contents, expected in cases:
        folder = root / name
        folder.mkdir()
        (folder / 'Incoming').mkdir()
        (folder / 'Temp').mkdir()
        source_file = folder / 'addresses.dat'
        if contents is not None:
            source_file.write_text(contents)
        ports = free_ports(3, udp_indices=(2,), peer_index=1)
        # ED2K is disabled, so checking the shipped default never contacts the Internet.
        write_amule_conf(folder, ports[0], Port=ports[1], UDPPort=ports[2], Ed2kServersUrl=legacy)
        proc, ec = start(binary, folder, ports)
        try:
            assert source_file.read_text() == expected, (name, source_file.read_text())
            assert 'Ed2kServersUrl=' not in (folder / 'amule.conf').read_text()
        finally:
            ec.sock.close()
            stop_daemon(proc)


def main():
    binary = str(Path(sys.argv[1]).resolve())
    api_binary = str(Path(sys.argv[2]).resolve()) if len(sys.argv) > 2 else None

    def attempt():
        with tempfile.TemporaryDirectory(prefix='amule-server-updates-') as temp:
            root = Path(temp)

            def case(name, responses, sources, expected, **kwargs):
                run(binary, root / name, responses, sources, expected, **kwargs)

            if api_binary:
                editor_case(binary, api_binary, root / 'editors')
                case('api-merge-all', {'/first': GOOD, '/last': SECOND}, ['/first', '/last'],
                     ['/first', '/last'], api_binary=api_binary)
                case('api-explicit-once', {'/alternate': GOOD}, ['/backup'], ['/alternate'],
                     url='/alternate', api_binary=api_binary)
                case('api-explicit-failure', {'/backup': GOOD}, ['/backup'], ['/alternate'],
                     url='/alternate', success=False, api_binary=api_binary)
                return

            migration_cases(binary, root)
            case('merge-after-success', {'/first': GOOD, '/last': SECOND}, ['/first', '/last'], ['/first', '/last'])
            case('first-run', {'/backup': GOOD}, ['/primary', '/backup'], ['/primary', '/backup'], startup=True)
            case('http-failure', {'/backup': GOOD}, ['/primary', '/backup'], ['/primary', '/backup'])
            case('invalid-download', {'/primary': (200, b'invalid'), '/backup': GOOD},
                 ['/primary', '/backup'], ['/primary', '/backup'])
            case('truncated-download', {'/primary': (200, TRUNCATED), '/backup': GOOD},
                 ['/primary', '/backup'], ['/primary', '/backup'])
            case('ordered-merge', {'/first': GOOD, '/last': SECOND},
                 ['/first', '/second', '/last', '/unused'], ['/first', '/second', '/last', '/unused'])
            case('all-fail', {}, ['/first', '/last'], ['/first', '/last'], success=False)
            case('truncated-only', {'/backup': (200, TRUNCATED)}, ['/backup'], ['/backup'], success=False)
            case('invalid-only', {'/backup': (200, b'invalid')}, ['/backup'], ['/backup'], success=False)
            case('missing-sources', {}, [], [], success=False, missing=True)
            case('empty-sources', {}, [], [], success=False)
            case('explicit-once', {'/alternate': GOOD}, ['/backup'], ['/alternate'], url='/alternate')
            case('explicit-fails-without-fallback', {'/backup': GOOD}, ['/backup'], ['/alternate'],
                 url='/alternate', success=False)
            case('deduplicated', {'/primary': GOOD, '/backup': GOOD},
                 ['/primary', '/primary', '# comment', 'ftp://unsupported/list', '/backup', '/backup'],
                 ['/primary', '/backup'])
            case('live-edit', {'/backup': GOOD}, ['/backup'], ['/backup'], reload=True)
            case('overlap-snapshot', {'/primary': GOOD, '/backup': GOOD}, ['/primary', '/backup'],
                 ['/primary', '/backup'], overlap=True)
            case('not-modified-continues', {'/primary': (304, b''), '/backup': GOOD},
                 ['/primary', '/backup'], ['/primary', '/backup'], skipped=True)
            case('invalid-requested-url', {'/backup': GOOD}, ['/backup'], [], url='invalid', success=False)
            case('invalid-sources', {}, ['# comment', 'ftp://unsupported/list', 'invalid'], [], success=False)
            case('migrate-custom-first', {'/custom': GOOD, '/backup': GOOD}, ['/backup', '/custom'],
                 ['/custom', '/backup'], legacy='/custom')
            case('cancel-manual-on-shutdown', {}, ['/primary', '/backup'], ['/primary'],
                 success=False, cancel=True)
            case('cancel-auto-on-shutdown', {}, ['/primary', '/backup'], ['/primary'],
                 success=False, cancel=True, automatic=True)
    retry_port_collisions(attempt)
    print('4 API/EC source-list scenarios passed' if api_binary else '22 server-list and 5 migration scenarios passed')


if __name__ == '__main__':
    main()
