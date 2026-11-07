#!/usr/bin/env python3
"""Private live regression: unavailable confinement disables the session, not files."""
import importlib.util
import configparser
from pathlib import Path
import shutil
import sys
import tempfile
import time

source, binary, fixture, evidence, role = map(Path, sys.argv[1:])
sys.path.insert(0, str(source / 'unittests/tests'))
from AllSearchIntegrationTest import C, free_ports, integer, running_daemon, retry_port_collisions
from DaemonConfig import daemon_env, write_amule_conf
spec = importlib.util.spec_from_file_location('packaged_runtime', source / 'packaging/media/test-runtime.py')
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


def run():
    with tempfile.TemporaryDirectory(prefix='amule-disable-regression-') as temporary:
        root = Path(temporary)
        core, binaries = root / 'core', root / 'bin'
        core.mkdir()
        binaries.mkdir()
        incoming = core / 'Incoming'
        incoming.mkdir()
        for index in range(3):
            media = incoming / f'sample-{index}.wav'
            runtime.make_wave(media)
            data = bytearray(media.read_bytes())
            data[50] = index
            media.write_bytes(data)
        daemon = shutil.copy2(binary, binaries / 'amuled')
        shutil.copy2(fixture, binaries / 'amule-media-probe')
        ec_port, peer_port, udp_port = free_ports(3, udp_indices=(2,), peer_index=1)
        write_amule_conf(core, ec_port, Port=peer_port, UDPPort=udp_port,
                         sections={'MediaMetadata': {'Enabled': 1}})
        trial = evidence / f'{role}-{time.monotonic_ns()}'
        trial.mkdir(parents=True)
        try:
            with running_daemon(daemon, core, (ec_port, peer_port, udp_port), env=daemon_env(root)) as (proc, ec):
                deadline = time.monotonic() + 45
                while True:
                    log = (core / 'logfile').read_text(errors='replace')
                    if 'controlled confinement setup failure' in log:
                        break
                    assert proc.poll() is None, 'daemon exited during setup failure'
                    assert time.monotonic() < deadline, 'no actual helper invocation observed'
                    time.sleep(0.1)
                _, tags = ec.call(C['EC_OP_GET_PREFERENCES'],
                                  [integer(C['EC_TAG_SELECT_PREFS'], C['EC_PREFS_FILES'])])
                files = tags[C['EC_TAG_PREFS_FILES']][1]
                enabled = files[C['EC_TAG_FILES_MEDIA_METADATA_ENABLED']][0]
                available = files[C['EC_TAG_FILES_MEDIA_METADATA_AVAILABLE']][0]
                assert enabled == 1, files
                if str(role) == 'current':
                    assert available == 0, files
                    assert log.count('Media metadata extraction disabled until restart:') == 1, log
                else:
                    assert available == 1, files
                    assert 'Media metadata extraction disabled until restart:' not in log, log
        finally:
            for name in ('console.log', 'logfile'):
                if (core / name).exists():
                    with (trial / name).open('x') as out:
                        out.write((core / name).read_text(errors='replace'))
        records = runtime.known_files(core / 'known.met')
        assert len(records) == 3, records
        for record in records:
            assert not record.get(b'\x57'), record
            assert not any(record.get(bytes([key])) for key in range(0xd0, 0xd6)), record
        saved = configparser.ConfigParser(interpolation=None)
        saved.optionxform = str
        saved.read(core / 'amule.conf')
        assert saved.getboolean('MediaMetadata', 'Enabled'), 'saved enable preference was changed'
        shutil.copy2(binary.with_name('amule-media-probe'), binaries / 'amule-media-probe')
        with running_daemon(daemon, core, (ec_port, peer_port, udp_port), env=daemon_env(root)) as (proc, ec):
            _, tags = ec.call(C['EC_OP_GET_PREFERENCES'],
                              [integer(C['EC_TAG_SELECT_PREFS'], C['EC_PREFS_FILES'])])
            files = tags[C['EC_TAG_PREFS_FILES']][1]
            assert files[C['EC_TAG_FILES_MEDIA_METADATA_AVAILABLE']][0] == 1, files
            assert files[C['EC_TAG_FILES_MEDIA_METADATA_ENABLED']][0] == 1, files
            deadline = time.monotonic() + 45
            while True:
                try:
                    recovered = runtime.known_files(core / 'known.met')
                except (OSError, ValueError):
                    recovered = []
                if len(recovered) == 3 and all(r.get(b'\xd5') == 'pcm_s16le' for r in recovered):
                    break
                assert proc.poll() is None, 'daemon exited while recovering after restart'
                assert time.monotonic() < deadline, recovered
                time.sleep(0.1)
        print(('PASS'  if str(role) == 'current' else 'EXPECTED OLD BEHAVIOR'),
              f'enabled={enabled}, available={available}; three files have no metadata or failure markers; restart recovers all three')


retry_port_collisions(run)
