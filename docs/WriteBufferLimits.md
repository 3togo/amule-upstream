# Download write-buffer backpressure

Only allocated write payload counts toward the 64 MiB global and 16 MiB
per-volume budgets. Selecting or sending block requests allocates no payload;
request metadata alone consumes no write capacity.
This leaves the normal source pipeline available regardless of source count
or round-trip time when storage keeps up.

Stop selecting new blocks at the budget and resume selection at 50%. Flush
small buffers early at 12.5%, leaving headroom for arrivals between main-thread
scheduling ticks. Reception queues early writes immediately, and the periodic
file tick also checks pressure. Successful writes return capacity immediately
in the worker.
Global pressure affects all volumes; local pressure affects only possible
aliases, including unresolved directory identities. The normal
file-buffer preference still controls batching below these thresholds.

Already requested responses remain admissible, including compressed responses.
The first missing fragment allocates its requested span, at most 180 KiB, and
charges that full allocation, including its unused holes. At most two immutable
write snapshots share the allocation. The charge ends when the last buffered
owner releases it; an outstanding request alone retains no payload. Duplicate
overlap is not buffered again. See [the metadata design](WriteMetadataLimits.md)
for bitmap costs and sparse read-modify-write amplification.
Write errors retain data and its charge for retry; disconnecting a source does
not release dirty data. Empty pipelines keep their upload slots and retry on
download ticks after storage drains.

The thresholds are not hard payload caps: outstanding responses can overshoot
them. During a complete stall, retained payload is bounded by the threshold
plus all outstanding requested ranges when selection stops (at most 24 blocks
of 180 KiB per downloading source). Slow sources can contribute their full
outstanding ranges, so this allowance is not necessarily rate times RTT.
Changing source count changes this bound. This does not promise a fixed total
RSS bound: packet/decompression scratch space, fragment metadata, mapped pages,
and the operating system's page cache are outside payload accounting.

Volume discovery runs in the write worker using the directory's device identity
(or Windows volume serial). Unknown paths conservatively share an allowance
and count against known volumes. A live remount keeps its old identity until
its payload drains. The single write worker still serializes volumes.


## Writes on separate filesystem volumes

The writer uses four bounded workers, with at most one active batch on a resolved
filesystem volume. Discovery is bounded separately and runs off the main thread.
An open file's cached descriptor identity avoids repeating a provider lookup;
unknown identities share a conservative lane. Main-thread request selection uses
the prepared volume hint while allocated payload accounting remains unchanged.

Queue limits are 4,096 accepted write jobs globally, 1,024 per volume, 64 discovery
jobs and 64 entries per batch. A refused queue entry remains owned by the part
file and is retried; it never becomes a synchronous main-thread write. Immutable
snapshots of one request remain serialized, including sparse read-modify-write.
Deletion waits for accepted work and shutdown drains owned jobs. A kernel call
that never returns can still prevent shutdown; workers are not detached.


## Coalescing dense snapshots

Adjacent dense snapshots of one file can share a scatter write, bounded to 16
ranges and 1 MiB. Sparse snapshots retain their individual read-modify-write path
so holes and earlier writes survive. Gaps and file boundaries split groups.
POSIX uses `writev`; Windows assembles a bounded temporary buffer. Short/failed
writes retain every original area and file pin for retry. Successful groups
release payload in the worker before main-thread result harvesting.

This reduces small-operation and seek costs when storage is slow. It also adds
batch bookkeeping and can lengthen an individual I/O operation. Throughput,
upload gaps, CPU, memory and actual read/write work need comparison under mixed
load. Fewer syscalls alone do not establish a benefit, durable writes or physical
HDD performance.
