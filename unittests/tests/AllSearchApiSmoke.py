#!/usr/bin/env python3
"""Run the curl All-search checks against an isolated, offline core/API pair."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from AllSearchIntegrationTest import connect_daemon, free_port, stored_search


def run(daemon_binary, api_binary, checks):
    with tempfile.TemporaryDirectory(prefix='amule-all-api-') as temporary:
        root = Path(temporary)
        core, api = root / 'core', root / 'api'
        core.mkdir()
        api.mkdir()
        port, http = free_port(), free_port()
        (core / 'amule.conf').write_text(f'''[eMule]
Nick=regression
Port={free_port()}
UDPEnable=0
Address=127.0.0.1
Autoconnect=0
ConnectToKad=0
ConnectToED2K=0
NewVersionCheck=0
Reconnect=0
Serverlist=0
Ed2kServersUrl=
KadNodesUrl=
AddServerListFromServer=0
AddServerListFromClient=0
UPnPEnabled=0
TempDir={core}/Temp
IncomingDir={core}/Incoming
[ExternalConnect]
AcceptExternalConnections=1
ECAddress=127.0.0.1
ECPort={port}
ECPassword={hashlib.md5(b'regression').hexdigest()}
[WebServer]
Enabled=0
''')
        # A finished All search exercises discovery and progress serialization
        # without any public peer, enabled network, or search-start skip path.
        (core / 'StoredSearches.met').write_bytes(stored_search([]))
        env = dict(os.environ, HOME=str(root), XDG_CONFIG_HOME=str(root / 'xdg'))
        processes = []
        with (root / 'fixture.log').open('w') as log:
            try:
                daemon = subprocess.Popen([daemon_binary, '-c', str(core)],
                                          env=env, stdout=log, stderr=log)
                processes.append(daemon)
                connect_daemon(daemon, port).sock.close()
                subprocess.run([api_binary, f'--config-dir={api}',
                                '--set-admin-pass=adminpass'], check=True,
                               env=env, stdout=log, stderr=log)
                config = api / 'amuleapi.conf'
                config.write_text(config.read_text().replace(
                    'Password=', 'Password=regression', 1))
                server = subprocess.Popen([api_binary, f'--config-dir={api}',
                                           '--host=127.0.0.1', f'--port={port}',
                                           f'--http-port={http}'],
                                          env=env, stdout=log, stderr=log)
                processes.append(server)
                url = f'http://127.0.0.1:{http}/api/v1'
                # Startup can include expensive password hashing. Let the
                # caller impose a deadline, while detecting process failure.
                while True:
                    if any(proc.poll() is not None for proc in processes):
                        raise RuntimeError('fixture process exited during startup')
                    if subprocess.run(['curl', '-sf', '--max-time', '2', url + '/health'],
                                      stdout=subprocess.DEVNULL).returncode == 0:
                        break
                    time.sleep(0.1)
                subprocess.run(['bash', checks, '--fixture-ready'], check=True,
                               env=dict(env, API=url, ALL_SID='123'))
            except BaseException:
                log.flush()
                print((root / 'fixture.log').read_text()[-12000:], file=sys.stderr)
                raise
            finally:
                for proc in reversed(processes):
                    if proc.poll() is None:
                        proc.terminate()
                    try:
                        proc.wait(timeout=30)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.wait()


if __name__ == '__main__':
    run(*sys.argv[1:])
