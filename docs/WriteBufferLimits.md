# Download write-buffer limits

Download block selection reserves the full uncompressed range before a block
can enter a request packet. The fixed safety limits are 64 MiB globally and
16 MiB per filesystem volume, shared across files and sources. The existing
file-buffer preference still controls normal flush batching. At 75% pressure,
small buffers on the affected volume flush early; global pressure flushes all
volumes. Unknown identities conservatively count against possible aliases.
Early flushing remains active until usage falls to 50%, avoiding threshold
oscillation while leaving admission caps unchanged.

The request and its buffered fragments share ownership of one reservation.
Disconnecting or cancelling releases unsatisfied requests; queued data keeps its
credit until its last buffer is harvested or discarded. Write errors retain
both the data and its reservation for retry. Overlapping duplicate packets only
buffer the missing ranges. Compression does not reduce the reserved size.

Volume identity belongs to the actual data file, including file symlinks into
another filesystem. POSIX reuses the descriptor identity from the existing
open/create length query; reading that cache adds no filesystem call. Otherwise
bounded background discovery runs before reserving network data. Unknown files
share conservative capacity until discovery succeeds. Identity and free-space
paths are retained for the file session; replacing/remounting an open backing
file is not dynamically reclassified.

Four workers run at most one batch per resolved volume. A batch contains up to
64 buffers. Admission allows 4,096 data jobs globally and 1,024 per volume, plus
64 separate discovery jobs; at most two workers perform discovery concurrently.
These independent discovery allowances and oldest-eligible scheduling let a
cold healthy file identify its volume even while resolved volumes replenish
their write queues. Discovery keeps its two-call concurrency limit. Queue saturation
leaves buffers ready for retry without synchronous I/O on the main thread.
Free-space samples and writes run within their volume's batch, so a slow probe
cannot hold a shared coordinator lock or another volume's write lane.

Cancellation revokes the file session and strips queued ownership. Active
provider calls keep the file pinned until they return; result publication
precedes the last pin release. Closing/deleting an active file and shutdown
still wait for its kernel calls. Workers are joined after draining accepted
ownership and are never detached. More blocked operations than the bounded pool
can cover still require external recovery; unresolved paths cannot promise the
same isolation as volumes with known identities.

When credit is exhausted, sources remain in the downloading state and already
requested responses remain admissible. Empty pipelines retry on download ticks
after writes drain; disk pressure does not trigger the no-needed-parts takeover.

These limits bound admitted payload, including network responses still owed.
They do not bound total process RSS, packet/decompression scratch space, buffer
metadata, mapped pages, or the operating system's page cache. This stage adds
write-worker isolation; it does not add write coalescing or a read cache.


Adjacent heap-backed fragments from the same file are flushed with one bounded
scatter write (at most 16 fragments and 1 MiB). POSIX uses `writev`; Windows uses
a bounded temporary buffer with ordinary file handles. Gaps, file boundaries
and mapped areas split groups. Mapped areas retain their existing individually
synchronized failure/rewrite path. A short write fails the entire group: original
areas, credits and file pins survive until retry or ordinary completion handling.
Successful groups close their areas together; the main thread still harvests
results and releases credits. These bounds reduce syscall count without claiming
that writes have reached stable media or measuring physical HDD throughput.


Upload reads and download writes acquire a shared storage service permit before
issuing I/O. The earliest refill deadline competes with a dirty buffer's creation
time plus one second; neither class can bypass a waiting other class for more
than four service turns. A write turn contains at most 16 jobs and 1 MiB (a single
larger existing job remains indivisible). These are operation bounds, not a
promise to interrupt or time-limit a blocked kernel call. Pools keep waiting work
queued and poll shared eligibility at 10 ms while blocked, leaving other workers
available for independent storage.

Linux descriptor and mount-table identities use the same major:minor spelling.
Worker-side sysfs discovery groups partitions and single-device stacks by their
backing disk; multi-device/unsupported stacks are conservative unknown resources.
Memory filesystems retain independent identities. Updates reclassify queued work while
active calls retain both their previous resource and newly discovered alias
conflicts until completion; independent resources remain available. Windows local-volume GUIDs and POSIX
cached device identities match the corresponding upload discovery keys. A cold
read which discovers a different descriptor identity defers and reacquires its
permit before reading bytes. Native Windows/macOS behavior and HDD performance
still require their own validation.


A peer can send a normal 180 KiB request as thousands of tiny packets. Keeping a
separate allocation for each packet allowed metadata to grow even when payload
credits stayed under their limit. Reception now fills one payload allocation per
request. Two byte-validity maps describe the worker's immutable snapshot and the
bytes still arriving; they share that allocation and never overwrite bytes owned
by the other snapshot. A request may have at most one snapshot queued or running
and one mutable snapshot. Valid responses therefore retain space reserved before
the request was sent.

Admission also limits live requests to 4,096 globally and 1,024 per volume. This
bounds allocated write snapshots to 8,192 and 2,048 respectively, independently
of packet size. The two validity maps use at most 16 MiB globally, plus 64 KiB for rounding
small maps to whole words. Partial flushes use at most 1 MiB of temporary storage
per writer. The original 64 MiB/16 MiB payload limits are unchanged. These are bounds
on cache-owned payload and metadata structures, not a process-RSS limit; existing
file gap/corruption records, allocator overhead and kernel caches are separate.

A partial snapshot preserves unreceived bytes with one bounded read-modify-write
under the file mutex. It overlays only its valid bytes, and does not mark its
holes as received. Download assemblies use heap storage so unreceived holes
cannot be dirtied through mappings. Dense snapshots still use adjacent scatter
writes. Failed writes retain payload, validity maps and credits. Completed and
deferred snapshots are reaped/retried promptly; hashing waits for READY snapshots
as well as active writes.

Per-volume byte/request totals and their largest-known values update on ownership
changes. Admission no longer scans all historical file paths. File accounts and
empty volume indexes disappear when their last request/snapshot releases its
credit; the live file session supplies the identity for later requests. Requests
share a file account rather than copying its path into every lease.
