# Public Kad lookup latency comparison, 2026-10-30

The candidate produced faster first results on the warmer second pass, but this
run does **not** establish an overall latency improvement. Its mean latency
including searches without results was effectively unchanged, and its total
result yield was lower.

## Observed results

The comparison ran from **03:45:41 to 03:53:12 UTC**. Each binary performed 24
keyword searches: 12 keywords repeated after the binaries swapped profiles.

| Metric | Previous behavior | Candidate |
|---|---:|---:|
| Searches with results within the observation window | 17/24 | 16/24 |
| Median first-result latency, successful searches only | 5.01 s | 1.64 s |
| Mean first-result latency capped at 45 s, all searches | 18.04 s | 17.84 s |
| Routing requests recorded across all searches | 438 | 467 |
| Item requests recorded across all searches | 78 | 73 |
| Sum of per-search unique result counts | 6,546 | 6,105 |

The successful-search median excludes failures and therefore cannot alone
justify a speedup claim. Counting searches without results at the 45-second
limit, the paired candidate-minus-baseline mean was **-0.195 seconds**. A
bootstrap resampling the 12 keyword clusters, keeping both passes of each
keyword together, gave a descriptive 95% interval of **-1.71 to +2.60 seconds**.
That interval includes both an improvement and a regression.

| Pass | Previous: searches with results | Candidate: searches with results | Previous mean, capped at 45 s | Candidate mean, capped at 45 s |
|---|---:|---:|---:|---:|
| Initial pass | 5/12 | 4/12 | 31.11 s | 33.82 s |
| Warmer second pass, profiles swapped | 12/12 | 12/12 | 4.97 s | 1.87 s |

On the second pass the candidate was faster on all 12 paired searches; the mean
reduction was **3.10 seconds (62.4%)**. That is consistent with eliminating the
old search-wide three-second wait before dispatching useful item requests.
This explanation is an inference from the implementation and timings, not a
separate causal experiment. The second pass also returned fewer results in
total: 4,486 versus 5,170. Its routing request count was lower, 152 versus 163;
over both passes the candidate sent **6.6% more routing requests**.

## Revision scope

These measurements were collected on the original workspace implementation
before this draft PR was adapted onto newer upstream code. The fingerprints
identify the measured binaries; this is not a benchmark of the final PR head.
The measured candidate also included packet-tracking/flood changes submitted
separately. These observations do not isolate the lookup scheduler from that
combined patch set. The final PR receives separate local correctness/build
validation.

## Method

- Two isolated real `amuled` instances used public IPv4 Kad from the same host.
  There were no mocked peers. Both began with identical copies of the existing
  local `nodes.dat`; its SHA-256 is recorded in the raw results.
- The baseline source snapshot retained the workspace's unrelated changes and
  restored the six changed Kad source/header files to commit
  `948fdb6a1651a72dd59879eefa655776e2b3e0ba`. The candidate used the implementation
  under review. Source and binary fingerprints are in the manifests/results.
- Both used Release builds, Boost.Asio sockets, IPv4 Kad and
  `ENABLE_KAD_NODE_PROTECTION=OFF`, the current default. UPnP was compiled out.
  Thus this measures the new scheduling/diversity behavior in the default
  configuration, not the optional adaptive timeout estimator.
- Each pass had 90 seconds of warmup, then three batches of four concurrent
  keywords per client, each observed for approximately 45 seconds. Keyword
  launch order alternated between variants. The second pass reversed keyword
  order and swapped the binaries between the profiles, retaining each
  profile's identity, ports and persisted routing state.
- The keywords were ubuntu, debian, fedora, libreoffice, blender, inkscape, vlc,
  audacity, ffmpeg, freebsd, python and rust. Only metadata was requested.
  Shared-file counts remained zero. Incoming/temp directories were isolated;
  no downloads or file publications were started.
- Authenticated, loopback-only EC polling at 250 ms measured the first observed
  routing reply, item request, item reply and nonzero unique result count.
  The parser preserves repeated lookup/peer tags. All 48 search traces were
  observed, with zero omitted-peer counts.
- Routing and item request counts come from production lookup diagnostics.
  They are message counts, not wire-byte measurements. EC total transfer-byte
  counters are retained as raw state but are not used to claim UDP traffic
  volumes. Reported per-peer RTT values retain the maximum observed value per
  peer within each search; they are not independent packet samples.
- Both instances were shut down at completion. The user's normal configuration
  and package installation were not modified.

## Limits and interpretation

This is one host, one network and one short run. The same public IP served both
clients; peer rate limits, changing remote availability and shared network
conditions can affect both. Routing-table sizes differed and changed during the
run. Both instances ended each pass reporting UDP-firewalled status; the first
pass began before that classification had settled. First-observation timings
are quantized by polling, with sequential EC calls adding a small offset.
Win/loss summaries treat differences within 0.5 seconds as ties.

The build manifest also records baseline `ENABLE_MMAP=ON` versus candidate OFF,
and a C-only `_GNU_SOURCE` flag difference. No file transfers exercised mmap;
the relevant C++ flags matched. These are retained explicitly rather than
presented as bit-identical build configurations.

The evidence supports a **warm-lookup responsiveness improvement in this run**.
It does not establish improved cold lookup latency, result completeness or
public-network performance across other connections. In particular, the lower
result yield deserves investigation before presenting the scheduling change
as an unconditional improvement. Flood resistance was not stress-tested against
public peers; deterministic regression tests cover those policies.

## Data and reproduction

- [Raw observations](results.json)
- [Computed summary](summary.json)
- [Build/source manifest](build-manifest.json)
- [Manual benchmark](benchmark.py)
- [Analysis](analyze.py)

From the repository root, supply the two binaries and an existing seed file:

```sh
python3 docs/benchmarks/2026-10-30-kad-live/benchmark.py \
  --baseline /absolute/path/to/previous/amuled \
  --candidate /absolute/path/to/candidate/amuled \
  --seed /absolute/path/to/nodes.dat \
  --work /tmp/kad-live-comparison-new \
  --output docs/benchmarks/2026-10-30-kad-live/results.json
python3 docs/benchmarks/2026-10-30-kad-live/analyze.py
```

Use a fresh output directory/copy of the scripts to retain these observations
when repeating the run. The benchmark is opt-in and is not registered with
ctest or CI.
