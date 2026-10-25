# Buffered-write backpressure validation, 2026-10-25

The revised policy charges received, unwritten payload instead of outstanding
network requests. Early writes start directly on reception, and the worker
returns capacity immediately after a successful write frees the payload.
Missing-range handling for overlapping duplicate packets remains enabled.

## Healthy-storage throughput

The comparison used x86-64 Ubuntu 26.10 (Stonking Stingray), GCC 15.3.0,
wxWidgets 3.2.11 and CMake 4.3.4, with Release headless daemon builds.
Emulated sources served verifiable plain payloads from 2 GiB fixtures, with
per-source upload caps and delayed responses. Each sample measured eight seconds
after two seconds of warm-up. Both binaries used the same harness and settings.

| Scenario | Upstream MiB/s | Revised MiB/s |
|---|---:|---:|
| 10 sources, 8 MiB/s each, 50 ms delay | 78.17 | 78.09 |
| 10 sources, 8 MiB/s each, 250 ms delay | 31.44 | 31.38 |
| 100 sources, 30 KiB/s each, 100 ms delay | 2.9180 | 2.9181 |
| 10 files, one 8 MiB/s source each, 50 ms delay | 78.83 | 78.13 |
| 20 sources, 8 MiB/s each, 50 ms delay | 156.85 | 155.80 |

All five samples were within 1% of upstream, with no peer errors. These are
single samples from a local emulation, rather than repeated statistical trials
or measurements of a physical WAN. The delayed-source case retains upstream's
own pipeline behavior; the comparison does not claim to saturate every source
at every latency. Independent ARM64 measurements remain useful.

## Stalls, errors and recovery

`WriteBackpressureIntegrationTest` passed with plain and compressed responses,
overlapping duplicates, stalled writes, injected write failures, and recovery.
Sources kept their slots; requests stopped growing during the stall and errors,
and resumed after writes drained. EC remained responsive. A separate 30-second plain-transfer write stall also
passed: requests remained at 17,588,224 bytes, peak sampled RSS was 46,575,616
bytes (44.42 MiB), and transfers resumed after the injected write error cleared.

The two-source cases settled at about 17 MiB of requested payload and sampled
RSS peaks below 45 MiB. A 36-source case first withheld responses until requests
exceeded the old 16 MiB reservation cap, then released them into a stalled writer.
It settled at about 31.5 MiB requested and below 66 MiB RSS. RSS stayed within
the asserted payload-plus-pipeline envelope through stalls and write failures.
The larger allowance in this assertion includes 24 blocks per source and
32 MiB of scratch/metadata headroom; it is not a fixed process-RSS guarantee.

The request thresholds remain 64 MiB globally and 16 MiB per volume, with
selection resuming at 50%. Early flushing starts at 12.5%. Already requested
responses may exceed those thresholds. During a complete stall, retained payload
is bounded by the threshold plus the outstanding requested ranges when selection
stops. That allowance depends on source count and need not equal rate times RTT.
See [the policy description](WriteBufferLimits.md) for exclusions from accounting.

## Other checks and source identity

The final affected build and seven selected CTest entries passed: write-budget
unit tests, real-peer backpressure integration, file areas, part-file buffering,
download display rates, short block packets and callback cooldown. The nearby
file-auto-close, part-met writer, global-source queue and AICH search-result
checks also passed. No selected tests skipped. clang-format-18 and both
clang-tidy-18 tiers passed, with the modernization tier filtered against the
actual upstream base. LLVM 18 configurations omitted only the unsupported
`ExcludeHeaderFilterRegex` key, outside the source tree.

The tested functional revision is `24bafffe3f89f976587309f2be03597d9c09b74c`,
with tree `b19366fa89994c8c639c7080d9090d89eb06dc83`. The comparison baseline is
upstream `a45dc3fb69104c48171fe16a00ece2f892a487eb`. Source manifests checked
all 2,719 candidate and 2,714 baseline tracked files; the only archive difference
was Git's export substitution in `.git_archival.txt`. The candidate manifest's
SHA-256 is `e059cf7266eddf4356d9a7668b030c33e8c719394ce86ad0740380d9b364cb04`.
This report is a documentation-only addition after the functional revision.
