#!/usr/bin/env python3
# Copyright (c) 2026 aMule Team
# SPDX-License-Identifier: GPL-2.0-or-later
"""Manual, opt-in public-Kad crossover benchmark; never registered with ctest.

Only keyword metadata is requested. Profiles, incoming and temporary directories
are isolated under --work. No downloads, public publishing or host config edits.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
from datetime import datetime, timezone

SOURCE = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(SOURCE / 'unittests/tests'))
from AllSearchIntegrationTest import C, EC, exact, free_ports, integer

QUERIES = ['ubuntu', 'debian', 'fedora', 'libreoffice', 'blender', 'inkscape',
           'vlc', 'audacity', 'ffmpeg', 'freebsd', 'python', 'rust']
FIELDS = {'requests': 'REQUESTS', 'replies': 'REPLIES', 'item_requests': 'ITEM_REQUESTS',
          'item_replies': 'ITEM_REPLIES', 'reported_results': 'RESULTS', 'rtt_ms': 'RTT'}


def utc():
    return datetime.now(timezone.utc).isoformat()


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def parse_many(data, offset, count):
    """Preserve repeated lookup and peer tags, unlike the single-record test parser."""
    records = []
    for _ in range(count):
        name, kind, length = struct.unpack_from('!HBI', data, offset)
        offset += 7
        children = []
        if name & 1:
            n = struct.unpack_from('!H', data, offset)[0]
            offset += 2
            start = offset
            children, offset = parse_many(data, offset, n)
            length -= offset - start
        value = data[offset:offset + length]
        offset += length
        if kind in (2, 3, 4, 5):
            value = int.from_bytes(value, 'big')
        elif kind == 6:
            value = value.rstrip(b'\0').decode('utf-8')
        records.append((name >> 1, value, children))
    return records, offset


def lookups(ec):
    payload = bytes([C['EC_OP_GET_KAD_LOOKUPS']]) + b'\0\0'
    ec.sock.sendall(struct.pack('!II', 0x20, len(payload)) + payload)
    flags, length = struct.unpack('!II', exact(ec.sock, 8))
    assert flags == 0x20
    reply = exact(ec.sock, length)
    assert reply[0] == C['EC_OP_GET_KAD_LOOKUPS']
    return parse_many(reply, 3, struct.unpack_from('!H', reply, 1)[0])[0]


def values(tags):
    return {name: value for name, value, _ in tags}


def stats(ec):
    tags = ec.call(C['EC_OP_STAT_REQ'])[1]
    names = ['KAD_NODES', 'KAD_FIREWALLED_UDP', 'KAD_IN_LAN_MODE',
             'TOTAL_SENT_BYTES', 'TOTAL_RECEIVED_BYTES', 'SHARED_FILE_COUNT']
    return {name.lower(): tags.get(C['EC_TAG_STATS_' + name], (None, {}))[0] for name in names}


def save(path, result):
    tmp = path.with_suffix('.tmp')
    tmp.write_text(json.dumps(result, indent=2) + '\n')
    tmp.replace(path)


def make_profile(root, seed):
    root.mkdir(parents=True, exist_ok=True)
    ec, tcp, udp = free_ports(3, udp_indices=(2,), peer_index=1)
    shutil.copyfile(seed, root / 'nodes.dat')
    (root / 'amule.conf').write_text(f'''[eMule]
Nick=Kad latency benchmark
Port={tcp}
UDPPort={udp}
Address=
ConnectToKad=1
Autoconnect=0
ConnectToED2K=0
FilterLanIPs=1
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
ECPort={ec}
ECPassword={hashlib.md5(b'regression').hexdigest()}
[AmuleApi]
Enabled=0
''')
    return {'path': root, 'ec': ec}


def launch(binary, profile, block):
    root = profile['path']
    env = dict(os.environ)
    # Proxychains interposes libc sockets; the public UDP experiment uses the
    # normal network path. No persistent proxy or routing settings are changed.
    env.pop('LD_PRELOAD', None)
    env['XDG_CONFIG_HOME'] = str(root / 'xdg')
    log = (root / f'block-{block}.log').open('w')
    proc = subprocess.Popen([binary, '-c', str(root)], stdout=log, stderr=log, env=env)
    deadline = time.monotonic() + 45
    try:
        while True:
            if proc.poll() is not None:
                raise RuntimeError(f'daemon exited; see {log.name}')
            try:
                ec = EC(profile['ec'])
                break
            except (ConnectionRefusedError, EOFError):
                if time.monotonic() >= deadline:
                    raise
                time.sleep(.1)
        ec.sock.settimeout(5)
        assert ec.call(C['EC_OP_KAD_START'])[0] == C['EC_OP_NOOP']
        return {'proc': proc, 'ec': ec, 'log': log}
    except BaseException:
        proc.terminate()
        proc.wait(timeout=20)
        log.close()
        raise


def stop(client):
    try:
        client['ec'].sock.close()
    finally:
        client['proc'].terminate()
        try:
            client['proc'].wait(timeout=20)
        except subprocess.TimeoutExpired:
            client['proc'].kill()
            client['proc'].wait()
        client['log'].close()


def sample(client, running):
    for tag_name, _, children in lookups(client['ec']):
        if tag_name != C['EC_TAG_KAD_LOOKUP']:
            continue
        fields = values(children)
        if fields.get(C['EC_TAG_KAD_LOOKUP_TYPE']) != 3:
            continue
        query = fields.get(C['EC_TAG_KAD_LOOKUP_KEYWORD'])
        if query not in running:
            continue
        record = running[query]
        elapsed = time.monotonic() - record['_start']
        record['seen_trace'] = True
        record['omitted_peers'] = max(record['omitted_peers'], fields[C['EC_TAG_KAD_LOOKUP_OMITTED_PEERS']])
        for name, _, tags in children:
            if name != C['EC_TAG_KAD_LOOKUP_PEER']:
                continue
            peer = values(tags)
            key = (peer[C['EC_TAG_KAD_LOOKUP_PEER_IP']], peer[C['EC_TAG_KAD_LOOKUP_PEER_PORT']])
            counters = record['_peers'].setdefault(key, {})
            for label, suffix in FIELDS.items():
                counters[label] = max(counters.get(label, 0), peer[C['EC_TAG_KAD_LOOKUP_PEER_' + suffix]])
        aggregate = {label: sum(p.get(label, 0) for p in record['_peers'].values())
                     for label in FIELDS if label != 'rtt_ms'}
        record.update(aggregate)
        for metric, trigger in [('first_routing_reply_s', 'replies'),
                                ('first_item_request_s', 'item_requests'),
                                ('first_item_reply_s', 'item_replies')]:
            if record[metric] is None and aggregate[trigger] > 0:
                record[metric] = elapsed
        rtts = [p['rtt_ms'] for p in record['_peers'].values() if p.get('replies', 0)]
        record['peer_rtt_ms'] = rtts
    for record in running.values():
        progress = client['ec'].progress(record['search_id'])
        count = progress.get(C['EC_TAG_SEARCH_RESULT_COUNT'], (0, {}))[0]
        record['unique_results'] = max(record['unique_results'], count)
        if count and record['first_result_s'] is None:
            record['first_result_s'] = time.monotonic() - record['_start']


def clean(record):
    return {k: v for k, v in record.items() if not k.startswith('_')}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--baseline', required=True)
    ap.add_argument('--candidate', required=True)
    ap.add_argument('--seed', required=True)
    ap.add_argument('--work', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--warmup', type=float, default=90)
    ap.add_argument('--duration', type=float, default=45)
    ap.add_argument('--poll', type=float, default=.25)
    args = ap.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    binaries = {'baseline': str(Path(args.baseline).resolve()), 'candidate': str(Path(args.candidate).resolve())}
    profiles = [make_profile(args.work / name, args.seed) for name in ['profile-a', 'profile-b']]
    result = {'started_utc': utc(), 'seed_sha256': sha(args.seed),
              'binary_sha256': {k: sha(v) for k, v in binaries.items()},
              'warmup_s': args.warmup, 'observation_s': args.duration,
              'poll_s': args.poll, 'max_concurrent_keywords_per_client': 4,
              'queries': QUERIES, 'blocks': [], 'records': []}
    save(args.output, result)
    for block in range(2):
        assignment = {'baseline': block, 'candidate': 1 - block}
        clients = {}
        block_record = {'block': block, 'started_utc': utc(), 'profiles': assignment, 'warmup': []}
        result['blocks'].append(block_record)
        try:
            for variant in ['baseline', 'candidate']:
                clients[variant] = launch(binaries[variant], profiles[assignment[variant]], block)
            until = time.monotonic() + args.warmup
            while time.monotonic() < until:
                snapshot = {'utc': utc(), **{k: stats(v['ec']) for k, v in clients.items()}}
                block_record['warmup'].append(snapshot)
                save(args.output, result)
                print('WARMUP', block, snapshot, flush=True)
                time.sleep(min(10, max(0, until - time.monotonic())))
            block_record['stats_before'] = {k: stats(v['ec']) for k, v in clients.items()}
            assert all(s['shared_file_count'] == 0 for s in block_record['stats_before'].values())
            queries = list(QUERIES)
            if block:
                queries.reverse()
            for batch in range(3):
                running = {k: {} for k in clients}
                for q_index, query in enumerate(queries[batch * 4: batch * 4 + 4]):
                    order = ['baseline', 'candidate'] if (batch + block + q_index) % 2 == 0 else ['candidate', 'baseline']
                    for variant in order:
                        start = time.monotonic()
                        sid = clients[variant]['ec'].start(query, kind=C['EC_SEARCH_KAD'])
                        record = {'block': block, 'batch': batch, 'variant': variant, 'query': query,
                                  'search_id': sid, 'started_utc': utc(), 'seen_trace': False,
                                  'first_routing_reply_s': None, 'first_item_request_s': None,
                                  'first_item_reply_s': None, 'first_result_s': None,
                                  'unique_results': 0, 'omitted_peers': 0,
                                  '_start': start, '_peers': {}}
                        record.update({k: 0 for k in FIELDS if k != 'rtt_ms'})
                        running[variant][query] = record
                until = max(r['_start'] for v in running.values() for r in v.values()) + args.duration
                while time.monotonic() < until:
                    cycle = time.monotonic()
                    for variant, client in clients.items():
                        sample(client, running[variant])
                    time.sleep(max(0, args.poll - (time.monotonic() - cycle)))
                for variant, client in clients.items():
                    sample(client, running[variant])
                    for record in running[variant].values():
                        record['observed_s'] = time.monotonic() - record['_start']
                        result['records'].append(clean(record))
                        client['ec'].call(C['EC_OP_SEARCH_STOP'], [integer(C['EC_TAG_SEARCH_ID'], record['search_id'])])
                        print('RESULT', clean(record), flush=True)
                save(args.output, result)
            block_record['stats_after'] = {k: stats(v['ec']) for k, v in clients.items()}
            block_record['finished_utc'] = utc()
        finally:
            for client in clients.values():
                stop(client)
            save(args.output, result)
    result['finished_utc'] = utc()
    save(args.output, result)


if __name__ == '__main__':
    main()
