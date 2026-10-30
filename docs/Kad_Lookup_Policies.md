# Kad lookup diversity and request deadlines

## Lookup diversity and scheduling

All local admission and timing constraints come from the validated,
application-wide [`[Kad]` policy](Kad_Constraints.md). The same IP/subnet settings
apply to routing admission and to referrals across the complete lookup. Initial
routing-table contacts are counted; LAN subnet exemptions are preserved.

Each request reserves capacity from a manager-owned budget before it is sent.
`LookupActiveGlobal` limits active requests across every search, and
`LookupPendingGlobal` limits all retained request records, including late-reply
records. Per-search concurrency/pending settings must fit those aggregate
budgets. Automatic aggregate budgets derive from the existing connection and
source-search allowances and the outgoing tracking capacity.

Deadlines and late-reply expiry are fixed when a request is reserved. Completion,
cancellation, expiry, failed sends and stopped searches return their reservations.
An idle search cannot pin expired aggregate capacity. Periodic fallback rotates
the first serviced target to avoid systematically favoring one search under a
small aggregate allowance. Deferred sends remain eligible candidates; a local
capacity refusal does not mark a contact tried or consume the widening budget.

`RequestTimeoutMs` supplies the fixed deadline and background quiet interval.
The existing optional adaptive estimator may shorten interactive deadlines.
`LateReplyGraceMs`, `WideningReasks`, node concurrency and all pending/concurrency
budgets are configured through the shared policy; none is a helper-local fixed
limit. Protocol-defined contact counts and wire representations remain separate.
`WideningReasks=0` disables both user-triggered widening and the automatic
dead-node fallback; both paths check the allowance before sending.

## Reference influence

The lookup-wide subnet history and distinction between soft timeout and late
response handling were informed by [libtorrent's traversal implementation at
3da533d](https://github.com/arvidn/libtorrent/blob/3da533d27e3c66af75aedc0ff7d44d68622a7e2b/src/kademlia/traversal_algorithm.cpp).
Libtorrent uses a different DHT protocol; its packets, subnet thresholds and
branching rules were not transplanted.

## Validation

`KadLookupPolicyTest` covers diversity across replies, seeds, LAN exceptions,
independent deadlines, endpoint matching, late samples, duplicate completion,
pending-record bounds, aggregate accounting across multiple searches, reservation
release on stop/cancellation and expiry boundaries. Existing daemon EC lookup and
index lifecycle tests exercise diagnostics and publication/search compatibility.
`KadLookupSchedulingIntegrationTest` observes real UDP requests with widening
disabled and enabled. It uses only owned addresses and skips when no owned
Kad-compatible IPv4 address is available.

The [public-network comparison](benchmarks/2026-10-30-kad-live/README.md) records
24 paired searches from the original workspace implementation: faster first
results on its warmer second pass, but no established overall improvement and
lower total result yield. Its exact source/binary fingerprints are retained;
it predates adaptation of this PR onto the newer upstream revision.

This revision depends on the shared policy introduced in draft #2213. The public
benchmark predates this configurable, aggregate-budget revision and is retained
as historical evidence, not a measurement of the current head.
