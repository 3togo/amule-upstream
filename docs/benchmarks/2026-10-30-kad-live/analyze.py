#!/usr/bin/env python3
# Copyright (c) 2026 aMule Team
# SPDX-License-Identifier: GPL-2.0-or-later
"""Summarize paired runs, retaining failed searches as censored observations."""
import json
from pathlib import Path
import random
import statistics

ROOT = Path(__file__).resolve().parent
raw = json.loads((ROOT / 'results.json').read_text())
records = raw['records']
limit = raw['observation_s']
summary = {'complete': 'finished_utc' in raw, 'started_utc': raw['started_utc'],
           'finished_utc': raw.get('finished_utc'), 'observation_s': limit,
           'variants': {}, 'paired': {}, 'blocks': {}}
metrics = ['first_routing_reply_s', 'first_item_request_s', 'first_item_reply_s', 'first_result_s']
for variant in ['baseline', 'candidate']:
    rows = [r for r in records if r['variant'] == variant]
    stats = {'searches': len(rows), 'trace_seen': sum(r['seen_trace'] for r in rows),
             'traces_with_omitted_peers': sum(bool(r['omitted_peers']) for r in rows)}
    for metric in metrics:
        completed = [r[metric] for r in rows if r[metric] is not None]
        capped = [min(r[metric], limit) if r[metric] is not None else limit for r in rows]
        stats[metric] = {'observed': len(completed),
                         'successful_median': statistics.median(completed) if completed else None,
                         'capped_median': statistics.median(capped) if capped else None,
                         'capped_mean': statistics.mean(capped) if capped else None}
    for field in ['requests', 'replies', 'item_requests', 'item_replies', 'unique_results']:
        vals = [r[field] for r in rows]
        stats[field] = {'sum': sum(vals), 'median': statistics.median(vals) if vals else None,
                        'mean': statistics.mean(vals) if vals else None}
    rtts = [v for r in rows for v in r.get('peer_rtt_ms', [])]
    stats['peer_rtt_ms_median'] = statistics.median(rtts) if rtts else None
    summary['variants'][variant] = stats
pairs = {}
for r in records:
    pairs.setdefault((r['block'], r['query']), {})[r['variant']] = r
rng = random.Random(20261030)
for metric in metrics:
    deltas = []
    clusters = {}
    for (_, query), pair in pairs.items():
        if len(pair) != 2:
            continue
        base, candidate = pair['baseline'][metric], pair['candidate'][metric]
        delta = ((min(candidate, limit) if candidate is not None else limit)
                 - (min(base, limit) if base is not None else limit))
        deltas.append(delta)
        clusters.setdefault(query, []).append(delta)
    if not deltas:
        continue
    cluster_values = list(clusters.values())
    boot = sorted(statistics.mean(v for cluster in rng.choices(cluster_values, k=len(cluster_values)) for v in cluster)
                  for _ in range(10000))
    # Each endpoint is polled in sequence. Differences within two poll periods
    # cannot support an ordinal win/loss claim.
    tolerance = 2 * raw['poll_s']
    wins, losses = sum(d < -tolerance for d in deltas), sum(d > tolerance for d in deltas)
    n = wins + losses
    summary['paired'][metric] = {'pairs': len(deltas), 'candidate_faster': wins,
                                 'candidate_slower': losses, 'ties': len(deltas)-n,
                                 'mean_candidate_minus_baseline_s': statistics.mean(deltas),
                                 'median_candidate_minus_baseline_s': statistics.median(deltas),
                                 'query_clusters': len(clusters),
                                 'query_cluster_bootstrap_mean_difference_95pct': [boot[249], boot[9749]],
                                 'win_loss_tolerance_s': tolerance}
for block in sorted({r['block'] for r in records}):
    summary['blocks'][block] = {}
    for variant in ['baseline', 'candidate']:
        rows = [r for r in records if r['block'] == block and r['variant'] == variant]
        if not rows:
            continue
        summary['blocks'][block][variant] = {
            'searches': len(rows), 'with_results': sum(r['first_result_s'] is not None for r in rows),
            'first_result_capped_mean_s': statistics.mean(min(r['first_result_s'], limit)
                                                        if r['first_result_s'] is not None else limit for r in rows),
            'routing_requests': sum(r['requests'] for r in rows),
            'unique_results': sum(r['unique_results'] for r in rows)}
(ROOT / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary, indent=2))
