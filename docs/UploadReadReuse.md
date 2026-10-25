# Measure upload reuse before adding a shared cache

A repeated request does not necessarily cause another physical disk read. The
operating system can serve it from its page cache. Upload heap reads already
hand their original allocation to packet construction. Another payload cache
needs evidence that its saved work justifies its memory, ownership rules and
invalidation cost.

Enabling the FileIO debug category adds range observations to the existing
`Upload storage:` summary. `range_reads` and `range_bytes` count successful file
reads. `exact_repeat_reads` and `exact_repeat_bytes` count ranges seen before;
`overlap_reads` and `overlap_bytes` include exact repeats and count the union of
previously covered bytes, without counting overlapping history entries twice.
The history retains at most 256 distinct ranges, refreshing exact repeats and
evicting the least recently used entry. `reuse_evictions` shows when that bound
limits the observation window. Interval summaries reset counters but retain the
history; changing logging state clears both.

These aggregate observations store content hashes and offsets, with no payload
or paths. They run only when diagnostics are enabled and add no clock reads.
They count successful storage reads even if a disconnected upload session later
discards the result. They do not count duplicate requests rejected before I/O,
and they cannot distinguish page-cache hits from physical device reads.

On Linux, the opt-in `unittests/tests/UploadReadReuseBenchmark.py` probe compares
those counters with actual file-read calls and `/proc/<pid>/io` physical-read
accounting. Give it the daemon, `libUploadReadDelay.so`, a dedicated disk-backed
`--workdir`, and optionally a new `--json` report path. It uses an owned random
32 MiB file and six independent upload sessions: a cold range, three exact
repeats, a half-block overlap, and a distant cold range. Every returned byte and
read offset is checked. Only this fixture's clean pages receive `DONTNEED` after
startup hashing; it never drops global caches. The cold controls must account
for physical reads, so a tmpfs run cannot masquerade as a disk measurement.

The report records the source revision, binary hash, OS, requested/read ranges,
elapsed times, logical reuse and physical-read deltas. The probe is synthetic:
field reuse frequency, contention, compression cost and HDD behavior still need
separate measurements. A high logical-repeat count with no additional physical
reads supports retaining the OS page cache for that workload. It does not prove
that a shared cache could never save CPU, nor justify adding one without a
measured benefit under representative load.


A slow device is a primary reason to absorb service-time variation with buffers.
A useful comparison therefore includes cold working sets, limited bandwidth,
seek latency and simultaneous reads/writes. It measures delivered throughput and
delivery gaps as well as CPU, retained memory, physical reads and write
amplification. A fast SSD result quantifies cost in that workload; it cannot
establish that another buffer is redundant on slow storage. Conversely, unit
checks of ownership and bounds do not establish a performance benefit on either.
A sustained bandwidth deficit also cannot be repaired by finite buffering;
backpressure, fairness and recovery remain separate requirements.

## Measurements on 25 October 2026

The observer was compared with its exact diagnostics-only parent, rather than
with a stack that also changes read-ahead or download scheduling. Both were
fresh Release builds on x86-64 Ubuntu 26.10 (Stonking Stingray), GCC 15.3.0 and
wxWidgets 3.2.11. The observer source was `7014b84aed1a`; its parent was
`c238087a8b10`. Later changes in this PR update documentation only.

Three loopback peers requested and verified every uploaded byte. Each row below
is the median of two interleaved A/B pairs after five seconds of warmup and eight
seconds of measurement. CPU is daemon CPU time per uploaded MiB. RSS is sampled
process memory, including more than the observer. The fast cases used a 64 MiB/s
network cap and warm native SSD pages. All cases used the same I/O tracing shim;
its overhead is common to both variants. These short samples are not confidence
intervals or a claim of zero overhead.

| Workload | Parent / observer MiB/s | Parent / observer CPU ms/MiB | Parent / observer RSS MiB |
| --- | ---: | ---: | ---: |
| Fast storage, diagnostics off | 49.54 / 49.07 | 5.19 / 5.09 | 50.85 / 51.15 |
| Fast storage, diagnostics on | 48.94 / 49.31 | 5.17 / 5.16 | 51.02 / 51.12 |
| Slow service with headroom, diagnostics on | 4.01 / 4.02 | 19.12 / 19.84 | 50.84 / 51.03 |

The last case imposed 8 MiB/s storage bandwidth, an 8 ms seek cost and 1 ms per
read, with a 4 MiB/s network cap. It models service time on actual SSD-backed
files; it is not a physical HDD measurement. Logging was checked: the enabled
observer emitted range counters, and the disabled category emitted no storage
summary. The measured memory difference was about 0.2–0.3 MiB in these samples;
other workloads and longer sessions still need measurement.

A separate owned-file probe read a 32 MiB file through the real daemon, with
mmap disabled. Six rounds each requested 1,474,560 bytes in eight ranges. The
first and distant cold rounds each caused 1,703,936 physical read bytes according
to `/proc` accounting. Three exact-repeat rounds and an overlapping round caused
zero additional physical read bytes, despite real file reads. The observer
reported 48 reads, 24 exact repeats, 32 overlaps and zero history evictions.
Thus logical reuse was already served by the OS cache in that fixture. Round
elapsed time includes connection and upload-slot setup and does not measure disk
latency.

These results support keeping the bounded observer available for gathering
representative evidence. They do not establish that an additional payload cache
would be redundant on a slow device, nor that it would be beneficial. A payload
cache proposal still needs avoided physical I/O, cache-state and reuse-distance
measurements under slow service, along with its memory, CPU and copy costs.
