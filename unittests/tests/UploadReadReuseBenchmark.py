#!/usr/bin/env python3
# Copyright (c) 2026 aMule Team
# SPDX-License-Identifier: GPL-2.0-or-later
"""Measure real repeated uploads and process physical reads on an owned regular file.

Run on a disk-backed workdir. Only this fixture's clean pages receive DONTNEED;
no global caches are dropped. This is an opt-in probe, not a normal CI test.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time
from AllSearchIntegrationTest import connect_daemon, free_ports, retry_port_collisions, stop_daemon
from DaemonConfig import write_amule_conf
from ShareWatcherMoveIntegrationTest import shared
from UploadStorageDelayIntegrationTest import BLOCK, Peer


def process_io(pid):
    return {key: int(value) for key, value in
            (line.split(':', 1) for line in Path(f'/proc/{pid}/io').read_text().splitlines())}


def run(binary, fixture, workdir):
    with tempfile.TemporaryDirectory(prefix='amule-reuse-', dir=workdir) as directory:
        root = Path(directory)
        incoming = root / 'Incoming'
        incoming.mkdir()
        (root / 'Temp').mkdir()
        data = os.urandom(32 * 1024 * 1024)
        target = incoming / 'reuse-probe.bin'
        with target.open('wb') as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        trace, arm = root / 'read-trace', root / 'armed'
        ports = free_ports(2, peer_index=1)
        write_amule_conf(root, ports[0], Port=ports[1], UDPEnable=0, FilterLanIPs=0,
                         MaxUpload=3500, SlotAllocation=100, MMapEnabled=0, IncomingDir=incoming,
                         SharedFileReverifyDays=0, VerboseDebug=1, sections={'Debug': {'Cat_FileIO': 1}})
        env = dict(os.environ, HOME=str(root), XDG_CONFIG_HOME=str(root / 'xdg'), LC_ALL='C.UTF-8',
                   LD_PRELOAD=':'.join(filter(None, [os.getenv('AMULE_TEST_PRELOAD_RUNTIME'), fixture])),
                   AMULE_TEST_READ_STAGE='trace', AMULE_TEST_READ_ARM=str(arm),
                   AMULE_TEST_READ_PATH=str(target), AMULE_TEST_READ_TRACE=str(trace))
        proc = ec = None
        rounds = []
        try:
            with (root / 'console.log').open('w') as log:
                proc = subprocess.Popen([binary, '-c', str(root), '--disable-fatal'], env=env,
                                        stdout=log, stderr=log)
            ec = connect_daemon(proc, ports[0], root, ports[1])
            deadline = time.monotonic() + 40
            files = {}
            while target.name not in files:
                assert time.monotonic() < deadline, 'fixture share did not finish hashing'
                files = shared(ec)
                time.sleep(0.05)
            # Startup hashing warmed this file. Evict only these clean, owned
            # pages after hashing has completed, before the cold control.
            with target.open('rb') as owned:
                os.posix_fadvise(owned.fileno(), 0, 0, os.POSIX_FADV_DONTNEED)
            arm.touch()
            ranges = [(i * BLOCK, (i + 1) * BLOCK) for i in range(8)]
            overlap = [(start + BLOCK // 2, end + BLOCK // 2) for start, end in ranges]
            distant = [(16 * 1024 * 1024 + start, 16 * 1024 * 1024 + end) for start, end in ranges]
            scenarios = [('cold', ranges), ('repeat-1', ranges), ('repeat-2', ranges),
                         ('repeat-3', ranges), ('overlap', overlap), ('distant-cold', distant)]
            for index, (name, requested) in enumerate(scenarios):
                before = process_io(proc.pid)
                old_trace = len(trace.read_text().splitlines()) if trace.exists() else 0
                start = time.monotonic()
                # A fresh upload session permits the same ranges to be requested
                # again; duplicates in one session's done-list are ignored.
                peer = Peer(ports[1], index, files[target.name])
                try:
                    peer.request(requested)
                    for span in requested:
                        peer.receive(span, data)
                finally:
                    peer.sock.close()
                after = process_io(proc.pid)
                actual = [tuple(map(int, line.split())) for line in trace.read_text().splitlines()[old_trace:]]
                expected = [(first, last - first) for first, last in requested]
                assert actual == expected, (name, actual, expected)
                result = dict(name=name, seconds=time.monotonic() - start,
                              requested_bytes=sum(last - first for first, last in requested),
                              actual_file_reads=len(actual), actual_ranges=actual,
                              physical_read_bytes=after['read_bytes'] - before['read_bytes'],
                              process_read_chars=after['rchar'] - before['rchar'])
                rounds.append(result)
                print(json.dumps(result), flush=True)
            assert rounds[0]['physical_read_bytes'] + rounds[-1]['physical_read_bytes'] > 0, \
                'cold controls caused no accounted disk reads; choose a disk-backed workdir'
            ec.sock.close()
            ec = None
            stop_daemon(proc)
            proc = None
            summaries = [dict((key, int(value)) for key, value in re.findall(r'\b(\w+)=(\d+)', line))
                         for line in (root / 'logfile').read_text().splitlines()
                         if 'Upload storage:' in line]
            counters = {key: sum(sample.get(key, 0) for sample in summaries)
                        for key in ('range_reads', 'range_bytes', 'exact_repeat_reads',
                                    'exact_repeat_bytes', 'overlap_reads', 'overlap_bytes', 'reuse_evictions')}
            assert counters['range_reads'] == 48 and counters['exact_repeat_reads'] == 24, counters
            assert counters['overlap_reads'] == 32, counters
            return dict(dataset_bytes=len(data), mmap=False, history_ranges=256,
                        rounds=rounds, production_reuse_counters=counters,
                        warm_repeat_physical_read_bytes=sum(row['physical_read_bytes'] for row in rounds[1:4]))
        except BaseException:
            print((root / 'console.log').read_text(errors='replace')[-5000:], flush=True)
            raise
        finally:
            if ec:
                ec.sock.close()
            if proc:
                stop_daemon(proc)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('fixture', type=Path)
    parser.add_argument('--workdir', type=Path, required=True)
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    assert args.workdir.is_dir(), 'create/inspect a dedicated workdir before running this probe'
    source = Path(__file__).resolve().parents[2]
    def git(*arguments):
        return subprocess.check_output(['git', *arguments], cwd=source, text=True).strip()
    revision = git('rev-parse', 'HEAD')
    assert not git('status', '--porcelain'), 'commit the candidate before measuring it'
    result = retry_port_collisions(run, str(args.binary.resolve()), str(args.fixture.resolve()), args.workdir.resolve())
    assert git('rev-parse', 'HEAD') == revision and not git('status', '--porcelain'), 'source changed during measurement'
    result.update(source_revision=revision,
                  binary_sha256=hashlib.sha256(args.binary.read_bytes()).hexdigest(),
                  os_release=Path('/etc/os-release').read_text())
    if args.json:
        with args.json.open('x') as output:
            json.dump(result, output, indent=2)
            output.write('\n')
    print(json.dumps(result, indent=2))
