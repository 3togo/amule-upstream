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

Snapshots use owned heap payloads even when the optional `MMapEnabled` preference
is enabled. The old mapped download path used synchronous `msync`; these writes
use `WriteAt`, like the existing default heap path. Equal-work comparisons must
keep mmap disabled on both sides, as it is by default, rather than attribute a
different writeback barrier to buffering performance. Compatibility with the
opt-in mapped mode and its writeback behavior needs a separate decision before
this proposal can be accepted.

The focused tests exercise shared allocation lifetime, two-snapshot ownership,
short/failed writes, sparse preservation, thousands of tiny packets and final
file integrity. Performance acceptance additionally needs equal-work comparisons
of ordinary and tiny packets, slow reads/writes, mixed contention, CPU, peak RSS
and bytes actually read/written. Physical HDD behavior remains a separate check.

## Validation and acceptance limits, 2026-10-25

On Ubuntu 26.10 (Stonking Stingray), x86-64, GCC 15.3.0 and wxWidgets 3.2.11,
the standalone Release headless build and four selected checks passed: file
areas, write budgets, real-peer backpressure/failure/recovery and tiny-fragment
completion. Focused Debug checks passed too. Removing only the asynchronous
deferral guard makes the same-peer ordering regression fail as intended.
The standalone tested revision is `833bf52f8137fddc1151cd19a959e0d25411df46`.

The combined metadata, volume, coalescing, shared-service and adaptive stack at
`e13e19bee1fa85ecbe6cb0a670544b0abd492974` also passed Debug and Release
ownership checks, compiled regression selections, GUI Release builds,
clang-format 18 and both clang-tidy 18 tiers. This is combined-stack coverage,
not an independently repeated full suite for each prerequisite.

Six combined-stack units passed AddressSanitizer plus UBSan. Three unchanged
live fixtures passed AddressSanitizer: tiny-fragment completion, volume isolation
and read/write service. The backpressure fixture passed its protocol, plateau,
write-error and recovery checks under AddressSanitizer with its two production
RSS assertions explicitly omitted. Those assertions passed in normal Debug and
Release builds; sanitizer allocator metadata and quarantine exceeded their
normal-allocator envelope without a reported memory error. This does not claim
that the unmodified backpressure fixture passed under AddressSanitizer.

Combined-stack UBSan live checks stopped at an inherited application-constructor
downcast before the feature tests ran. The focused fix is PR #2099 and is not
silently included in this stack. No clean combined-stack live UBSan result is
claimed. No sanitizer warning category was broadly suppressed for these checks.

Healthy-download performance of this metadata representation was not measured
in this review. The inherited backpressure report describes the #2078 accounting
prerequisite, not these snapshots. The separate upload/shared-service comparisons
also hold the write representation constant. Neither establishes that sparse
read-modify-write or full-span allocation is an acceptable download cost.
Keep this proposal and its dependent storage features in draft pending that
tradeoff and the optional mapped-mode decision above.
