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
