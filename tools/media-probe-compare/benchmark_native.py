#!/usr/bin/env python3
"""Benchmark shipped native helper against ffprobe; preserve exact differences.
No custom frame-counting correction is applied to any backend.
"""
import argparse
import json
import platform
from pathlib import Path
import statistics
import subprocess
import time
from compare import ENTRIES, normalize, clean


def execute(command, path, repeats, timeout):
    times, outputs = [], []
    for _ in range(repeats):
        with path.open('rb') as input_file:
            start = time.perf_counter()
            try:
                p = subprocess.run(command, stdin=input_file, capture_output=True, timeout=timeout)
                duration = (time.perf_counter() - start) * 1000
                output = {'exit': p.returncode}
                if p.returncode == 0:
                    output['data'] = json.loads(p.stdout)
                else:
                    output['error'] = p.stderr.decode('utf-8', 'replace')
            except subprocess.TimeoutExpired:
                duration = (time.perf_counter() - start) * 1000
                output = {'exit': -1, 'error': 'timeout'}
        times.append(duration); outputs.append(output)
    return {'median_ms': statistics.median(times), 'min_ms': min(times), 'max_ms': max(times),
            'repeatable': all(o == outputs[0] for o in outputs), 'output': outputs[0]}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('corpus', type=Path)
    p.add_argument('--helper', required=True, type=Path)
    p.add_argument('--ffprobe', default='ffprobe')
    p.add_argument('--repeats', type=int, default=5)
    p.add_argument('--timeout', type=float, default=30)
    p.add_argument('--report', type=Path, required=True)
    args = p.parse_args()
    if args.repeats < 1: p.error('repeats must be positive')
    report = {'helper_version': subprocess.check_output([str(args.helper), '--version']).decode().strip(),
              'platform': platform.platform(),
              'ffprobe_version': subprocess.check_output([args.ffprobe, '-version']).decode().splitlines()[0],
              'repeats': args.repeats, 'files': [], 'notes': 'warm-cache process wall time; no production 25ms polling overhead; not a size or cold-I/O benchmark'}
    for path in sorted(x for x in args.corpus.iterdir() if x.is_file()):
        backends = {name: execute([str(args.helper), '--backend', name, '--json'], path, args.repeats, args.timeout)
                    for name in ('mediainfo', 'libav')}
        backends['ffprobe'] = execute([args.ffprobe, '-v', 'error', '-show_entries', ENTRIES, '-of', 'json', str(path.resolve())], path, args.repeats, args.timeout)
        for name, data in backends.items():
            if data['output']['exit'] == 0:
                raw = data['output']['data']
                if name == 'ffprobe': data['result'] = normalize(raw)
                else:
                    data['result'] = dict(raw)
                    data['result'].pop('backend', None)
                    for key in ('artist', 'album', 'title'): data['result'][key] = clean(data['result'][key])
                    data['result']['outcome'] = 'Extracted'
        primary = backends['mediainfo'].get('result')
        fallback = backends['libav'].get('result')
        invoked = not primary or not primary['length_seconds'] or not primary['codec']
        selected = 'mediainfo'
        if invoked and fallback and (not primary or (
            (not primary['length_seconds'] or fallback['length_seconds']) and
            (not primary['codec'] or fallback['codec']) and
            ((not primary['length_seconds'] and fallback['length_seconds']) or (not primary['codec'] and fallback['codec'])))):
            selected = 'libav'
        chosen = backends[selected].get('result')
        reference = backends['ffprobe'].get('result')
        diff = {k: {'ffprobe': reference[k], 'selected': chosen[k]} for k in reference if chosen[k] != reference[k]} if chosen and reference else {'error': 'no usable result'}
        report['files'].append({'path': str(path), 'backends': backends, 'fallback_needed': invoked, 'selected': selected,
            'estimated_chain_ms': backends['mediainfo']['median_ms'] + (backends['libav']['median_ms'] if invoked else 0), 'differences': diff})
    report['summary'] = {'files': len(report['files']), 'matches_ffprobe': sum(not f['differences'] for f in report['files']),
                         'fallback_needed': sum(f['fallback_needed'] for f in report['files']),
                         'backend_median_ms': {n: statistics.median(f['backends'][n]['median_ms'] for f in report['files']) for n in ('mediainfo', 'libav', 'ffprobe')}}
    args.report.write_text(json.dumps(report, indent=2, ensure_ascii=True) + '\n')
    print(json.dumps(report['summary'], indent=2))

if __name__ == '__main__': main()
