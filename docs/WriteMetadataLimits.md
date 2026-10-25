# Bounded metadata for fragmented download responses

A source can send one requested block as thousands of tiny packets. Keeping one
write object and allocation for every packet makes bookkeeping grow with packet
count. This proposal instead keeps one block-span payload and at most two
validity maps: one immutable writer snapshot and one receiving snapshot.
Overlapping duplicate bytes remain excluded.

The dependency is the received-payload accounting in #2078. Outstanding network
requests keep metadata but allocate no payload. There is no new request-count
admission limit. The first received byte allocates the requested span, at most
180 KiB, and that entire allocation is charged until its final buffered owner
releases it. A request that remains in flight retains no payload after its
buffered writes drain. Later bytes allocate a fresh span if necessary.

The 64 MiB global and 16 MiB volume thresholds, early flushing at one eighth,
and resume-at-half rule are preserved. Already requested responses remain
admissible. These are admission thresholds, not absolute process-RSS caps;
in-flight requests, validity maps, socket buffers and scratch space add to them.
The number of outstanding requests still depends on source count and protocol
request depth. No claim of a fixed total-RSS ceiling follows from these tests.

## Costs that need measurement

A validity map costs one bit per byte of span, up to 22.5 KiB. Two maps and one
180 KiB payload therefore retain up to 225 KiB per live requested span, plus
object/allocator overhead. The writer can need another 180 KiB scratch buffer
for a partial span. This trades bounded per-block memory for the old unbounded
per-packet bookkeeping; ordinary large packets can use less memory in the old
representation.

A dense snapshot writes directly. A sparse snapshot reads existing bytes and
writes one merged span so holes and earlier writes remain intact. This can add
I/O and CPU work for small partial writes. It must be evaluated on slow storage,
where fewer small operations may help but read-modify-write amplification can
also hurt. Fast-storage throughput alone cannot establish this tradeoff.

The focused tests exercise shared allocation lifetime, two-snapshot ownership,
short/failed writes, sparse preservation, thousands of tiny packets and final
file integrity. Performance acceptance additionally needs equal-work comparisons
of ordinary and tiny packets, slow reads/writes, mixed contention, CPU, peak RSS
and bytes actually read/written. Physical HDD behavior remains a separate check.
